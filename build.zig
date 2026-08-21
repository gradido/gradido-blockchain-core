const std = @import("std");
const zcc = @import("compile_commands");

/// Recursively add .c files from a directory.
///
/// @p skip names what the walk leaves alone, relative to @p dir_path: a subdirectory, for a
/// vendored library that ships its own tests and tools beside the source it wants built, or a
/// single file, for a translation unit fragment that is included by others rather than compiled
/// on its own. Write its entries with '/' whatever the host is; the walk converts them to
/// the host separator before comparing.
fn addDirSources(
    lib: *std.Build.Step.Compile,
    b: *std.Build,
    dir_path: []const u8,
    skip: []const []const u8,
) void {
    var dir = b.build_root.handle.openDir(dir_path, .{ .iterate = true }) catch |err| {
        std.debug.panic("Failed to open directory '{s}': {s}", .{ dir_path, @errorName(err) });
    };
    defer dir.close();

    var walker = dir.walk(b.allocator) catch |err| {
        std.debug.panic("Failed to walk directory '{s}': {s}", .{ dir_path, @errorName(err) });
    };
    defer walker.deinit();

    // The walker joins components with the host separator, while the skip list is written with
    // '/' at the call sites. Convert once here rather than making every caller spell the host's
    // separator: on a posix host this copies the strings unchanged, on Windows it turns
    // "mapping/json_writer.c" into the "mapping\\json_writer.c" the walker actually reports.
    const skip_native = b.allocator.alloc([]const u8, skip.len) catch |err| {
        std.debug.panic("Failed to allocate the skip list: {s}", .{@errorName(err)});
    };
    for (skip, skip_native) |written, *native| {
        const copy = b.dupe(written);
        std.mem.replaceScalar(u8, copy, '/', std.fs.path.sep);
        native.* = copy;
    }

    outer: while (walker.next() catch null) |entry| {
        if (entry.kind == .file and std.mem.endsWith(u8, entry.path, ".c")) {
            for (skip_native) |skipped| {
                if (std.mem.eql(u8, entry.path, skipped)) continue :outer;
                const prefix = b.fmt("{s}{c}", .{ skipped, std.fs.path.sep });
                if (std.mem.startsWith(u8, entry.path, prefix)) continue :outer;
            }
            const full_path = b.fmt("{s}/{s}", .{ dir_path, entry.path });
            lib.addCSourceFiles(.{
                .files = &[_][]const u8{full_path},
                .flags = &.{},
            });
        }
    }
}

/// Sanitizers the zig toolchain can apply to the C/C++ sources of this project.
///
/// AddressSanitizer is deliberately absent: zig does not ship the asan runtime, so
/// `-fsanitize=address` would compile but fail to link. Use the CMake build with
/// `-DENABLE_SANITIZERS=ON` for a leak and out-of-bounds check.
const SanitizeMode = enum {
    /// no instrumentation
    off,
    /// UndefinedBehaviorSanitizer, aborting with a diagnostic on the first finding
    undefined_behavior,
    /// ThreadSanitizer, reporting data races between threads
    thread,
};

/// Instrument a module according to @p mode. Applied to every target of the build so that
/// library and test binary always agree on their instrumentation.
fn applySanitize(module: *std.Build.Module, mode: SanitizeMode) void {
    switch (mode) {
        .off => {},
        .undefined_behavior => module.sanitize_c = .full,
        .thread => module.sanitize_thread = true,
    }
}

const BuildContext = struct {
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    core_lib: *std.Build.Step.Compile,
    hostmem_dep: *std.Build.Dependency,
    googletest_dep: ?*std.Build.Dependency,
    sodium_dep: ?*std.Build.Dependency,
    singleOutputDir: bool,
    sanitize: SanitizeMode,
    cdb: *std.ArrayList(*std.Build.Step.Compile),
};

const BuildTarget = struct {
    link_googletest: bool = false,
    link_sodium: bool = false,
    name: []const u8,
    srcs: []const []const u8,
};

fn processBuildTarget(context: *const BuildContext, build_target: BuildTarget, path: []const u8) void {
    const b = context.b;
    const exe = b.addExecutable(.{
        .name = build_target.name,
        .root_module = b.createModule(.{
            .target = context.target,
            .optimize = context.optimize,
        }),
    });

    applySanitize(exe.root_module, context.sanitize);
    exe.linkLibrary(context.core_lib);

    if (build_target.link_googletest) {
        if (context.googletest_dep) |dep| {
            exe.linkLibrary(dep.artifact("gtest"));
            exe.linkLibrary(dep.artifact("gtest_main"));
        }
    }
    if (build_target.link_sodium) {
        exe.root_module.addCMacro("USE_SODIUM", "1");
        if (context.sodium_dep) |dep| {
            exe.linkLibrary(dep.artifact(if (context.target.result.os.tag == .windows) "libsodium-static" else "sodium"));
        }
    }

    exe.addIncludePath(b.path("include"));
    exe.addIncludePath(b.path("third_party"));
    exe.addIncludePath(b.path("third_party/yyjson/src"));
    exe.addIncludePath(context.hostmem_dep.path("include"));

    for (build_target.srcs) |src_file| {
        exe.addCSourceFiles(.{
            .files = &.{b.fmt("{s}/{s}", .{ path, src_file })},
        });
    }

    context.cdb.append(b.allocator, exe) catch @panic("OOM");

    if (context.singleOutputDir) {
        const bin_install_step = b.addInstallBinFile(exe.getEmittedBin(), b.fmt("../{s}", .{exe.out_filename}));
        b.getInstallStep().dependOn(&bin_install_step.step);
    } else {
        b.installArtifact(exe);
    }
}

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    // make a list of targets that have include files and c source files
    var cdbTargets: std.ArrayList(*std.Build.Step.Compile) = .empty;

    // Options
    const enable_benchmarks = b.option(bool, "benchmarks", "Enable benchmarks") orelse false;
    const enable_tests = b.option(bool, "tests", "Enable tests") orelse false;
    const enable_sodium = b.option(bool, "sodium", "Enable sodium and crypto") orelse false;
    const lib_shared = b.option(bool, "shared", "Make lib shared") orelse false;
    const singleOutputDir = b.option(bool, "singleOutputDir", "Put direct into output folder, without lib or bin folder") orelse false;
    const sanitize = b.option(SanitizeMode, "sanitize", "Instrument C sources: undefined_behavior (UBSan) or thread (TSan). AddressSanitizer needs the CMake build with -DENABLE_SANITIZERS=ON") orelse .off;

    const core_lib = b.addLibrary(.{ .name = "gradido_blockchain_core", .linkage = if (lib_shared) .dynamic else .static, .root_module = b.createModule(.{
        .target = target,
        .optimize = optimize,
    }) });
    applySanitize(core_lib.root_module, sanitize);

    // hostmem carries the allocator, the containers and the conversions this project used to
    // keep in src/utils. It is pulled in as a package, not vendored.
    const hostmem_dep = b.dependency("hostmem", .{ .target = target, .optimize = optimize });

    const context: BuildContext = .{
        .b = b,
        .target = target,
        .optimize = optimize,
        .core_lib = core_lib,
        .hostmem_dep = hostmem_dep,
        .googletest_dep = b.lazyDependency("googletest", .{
            .target = target,
            .optimize = optimize,
        }),
        .sodium_dep = b.lazyDependency("libsodium", .{
            .target = target,
            .optimize = optimize,
            .static = true,
            .shared = false,
        }),
        .singleOutputDir = singleOutputDir,
        .sanitize = sanitize,
        .cdb = &cdbTargets,
    };

    if (enable_sodium) {
        core_lib.root_module.addCMacro("USE_SODIUM", "1");
        if (context.sodium_dep) |dep| {
            core_lib.linkLibrary(dep.artifact(if (target.result.os.tag == .windows) "libsodium-static" else "sodium"));
        }
    }

    core_lib.linkLibC();
    core_lib.linkLibrary(hostmem_dep.artifact("hostmem"));
    core_lib.addIncludePath(hostmem_dep.path("include"));

    core_lib.addIncludePath(b.path("include"));
    core_lib.addIncludePath(b.path("include/gradido_blockchain_core/data/proto/gradido"));
    core_lib.addIncludePath(b.path("third_party"));
    core_lib.addIncludePath(b.path("third_party/pbtools"));
    core_lib.addIncludePath(b.path("third_party/yyjson/src"));

    // Translation unit fragments: the json mappings include these whole, so compiling them here
    // as well would build objects of unreachable statics. See their file comments for why they
    // are .c files and not headers.
    addDirSources(core_lib, b, "src", &.{ "mapping/json_writer.c", "mapping/json_arena_alc.c" });
    // yyjson is a submodule of the upstream repository, which ships its tests, fuzzers and
    // tools beside the library. The walk skips it whole and its one source is named here,
    // so a recursive sweep cannot pick up a second main() or a fuzzer entry point.
    addDirSources(core_lib, b, "third_party", &.{"yyjson"});
    core_lib.addCSourceFiles(.{
        .files = &.{"third_party/yyjson/src/yyjson.c"},
        .flags = &.{},
    });

    // keep track of it, so later we can pass it to compile_commands
    cdbTargets.append(b.allocator, core_lib) catch @panic("OOM");

    if (singleOutputDir) {
        const bin_install_step = b.addInstallBinFile(core_lib.getEmittedBin(), b.fmt("../{s}", .{core_lib.out_filename}));
        b.getInstallStep().dependOn(&bin_install_step.step);
        if (target.result.os.tag == .windows) {
            const lib_install_step = b.addInstallLibFile(core_lib.getEmittedImplib(), b.fmt("../{s}", .{core_lib.out_lib_filename}));
            b.getInstallStep().dependOn(&lib_install_step.step);
        }
    } else {
        b.installArtifact(core_lib);
    }
    if (enable_benchmarks) {
      const path = "benchmarks/src";
      processBuildTarget(&context, .{
          .link_googletest = false,
          .link_sodium = enable_sodium,
          .name = "bench_numberToString",
          .srcs = &.{"bench_numberToString.c"},
      }, path);
      processBuildTarget(&context, .{
          .link_googletest = false,
          .link_sodium = enable_sodium,
          .name = "bench_json",
          .srcs = &.{"bench_json.c"},
      }, path);
      if (enable_sodium) {
        processBuildTarget(&context, .{ .link_googletest = false, .link_sodium = true, .name = "bench_crypto", .srcs = &.{"bench_crypto.c"} }, path);
      }
    }

    if (enable_tests) {
        const path = "tests/unit/src";
        processBuildTarget(&context, .{ .link_googletest = true, .link_sodium = false, .name = "data", .srcs = &.{"test_data.cpp"} }, path);
        processBuildTarget(&context, .{ .link_googletest = true, .link_sodium = false, .name = "data_wire", .srcs = &.{"test_data_wire.cpp"} }, path);
        processBuildTarget(&context, .{ .link_googletest = true, .link_sodium = false, .name = "test_unit", .srcs = &.{"test_unit.cpp"} }, path);
        processBuildTarget(&context, .{ .link_googletest = true, .link_sodium = false, .name = "test_json", .srcs = &.{"test_json.cpp"} }, path);
        if (enable_sodium) {
            processBuildTarget(&context, .{ .link_googletest = true, .link_sodium = true, .name = "test_converter", .srcs = &.{"test_converter.cpp"} }, path);
            processBuildTarget(&context, .{ .link_googletest = true, .link_sodium = true, .name = "test_crypto", .srcs = &.{ "test_crypto.cpp", "utils.cpp" } }, path);
            processBuildTarget(&context, .{ .link_googletest = true, .link_sodium = true, .name = "test_pbtools", .srcs = &.{ "test_pbtools.cpp", "key_pairs.cpp" } }, path);
            processBuildTarget(&context, .{ .link_googletest = true, .link_sodium = true, .name = "test_runtime", .srcs = &.{ "test_runtime.cpp", "key_pairs.cpp" } }, path);
        }
    }

    const cdbTargetsSlice = cdbTargets.toOwnedSlice(b.allocator) catch @panic("OOM");
    const buildStep = zcc.createStep(b, "cdb", cdbTargetsSlice);
    // Build everything in the project before generating the compile_commands
    for (cdbTargetsSlice) |cdbTarget| buildStep.dependOn(&cdbTarget.step);
}
