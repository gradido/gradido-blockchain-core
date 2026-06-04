const std = @import("std");

/// Recursively add .c files from a directory
fn addDirSources(
    lib: *std.Build.Step.Compile,
    b: *std.Build,
    dir_path: []const u8,
) void {
    var dir = std.fs.cwd().openDir(dir_path, .{ .iterate = true }) catch |err| {
        std.debug.panic("Failed to open directory '{s}': {s}", .{ dir_path, @errorName(err) });
    };
    defer dir.close();

    var walker = dir.walk(b.allocator) catch |err| {
        std.debug.panic("Failed to walk directory '{s}': {s}", .{ dir_path, @errorName(err) });
    };
    defer walker.deinit();

    while (walker.next() catch null) |entry| {
        if (entry.kind == .file and std.mem.endsWith(u8, entry.path, ".c")) {
            const full_path = b.fmt("{s}/{s}", .{ dir_path, entry.path });
            lib.addCSourceFiles(.{
                .files = &[_][]const u8{full_path},
                .flags = &.{},
            });
        }
    }
}

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const enable_benchmarks = b.option(bool, "benchmarks", "Enable benchmarks") orelse false;
    const enable_tests = b.option(bool, "tests", "Enable tests") orelse false;

    const lib = b.addLibrary(.{
        .name = "gradido_blockchain_core",
        .linkage = .static,
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
        }),
    });

    lib.linkLibC();

    lib.addIncludePath(b.path("include"));
    lib.addIncludePath(b.path("include/gradido_blockchain_core/data/proto/gradido"));
    lib.addIncludePath(b.path("third_party"));
    lib.addIncludePath(b.path("third_party/pbtools"));

    addDirSources(lib, b, "src");
    addDirSources(lib, b, "third_party");

    b.installArtifact(lib);

    if (enable_benchmarks) {
        const bench = b.addExecutable(.{ .name = "bench_numberToString", .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
        }) });

        bench.linkLibrary(lib);
        bench.addIncludePath(b.path("include"));
        bench.addIncludePath(b.path("third_party"));

        bench.addCSourceFiles(.{
            .files = &.{"benchmarks/src/bench_numberToString.c"},
        });

        b.installArtifact(bench);
    }

    if (enable_tests) {
        const googletest_dep = b.lazyDependency("googletest", .{
            .target = target,
            .optimize = optimize,
        });

        const test_targets = [_]struct {
            name: []const u8,
            src: []const u8,
        }{
            .{ .name = "test_converter", .src = "tests/unit/src/test_converter.cpp" },
            .{ .name = "test_duration", .src = "tests/unit/src/test_duration.cpp" },
            .{ .name = "test_memory", .src = "tests/unit/src/test_memory.cpp" },
            .{ .name = "test_unit", .src = "tests/unit/src/test_unit.cpp" },
        };
        for (test_targets) |tt| {
            const exe = b.addExecutable(.{
                .name = tt.name,
                .root_module = b.createModule(.{
                    .target = target,
                    .optimize = optimize,
                }),
            });

            exe.linkLibrary(lib);

            if (googletest_dep) |dep| {
                exe.linkLibrary(dep.artifact("gtest"));
                exe.linkLibrary(dep.artifact("gtest_main"));
            }

            exe.addIncludePath(b.path("include"));
            exe.addIncludePath(b.path("third_party"));

            exe.addCSourceFiles(.{
                .files = &.{tt.src},
            });

            b.installArtifact(exe);
        }
        // test_pbtools
        const test_pbtools = b.addExecutable(.{
            .name = "test_pbtools",
            .root_module = b.createModule(.{
                .target = target,
                .optimize = optimize,
            }),
        });

        test_pbtools.linkLibrary(lib);
        if (b.lazyDependency("libsodium", .{
            .target = target,
            .optimize = optimize,
            .static = true,
            .shared = false,
        })) |dep| {
            test_pbtools.linkLibrary(dep.artifact(if (target.result.os.tag == .windows) "libsodium-static" else "sodium"));
        }
        if (googletest_dep) |dep| {
            test_pbtools.linkLibrary(dep.artifact("gtest"));
            test_pbtools.linkLibrary(dep.artifact("gtest_main"));
        }

        test_pbtools.addIncludePath(b.path("include"));
        test_pbtools.addIncludePath(b.path("third_party"));

        test_pbtools.addCSourceFiles(.{
            .files = &.{
                "tests/unit/src/test_pbtools.cpp",
                "tests/unit/src/key_pairs.cpp",
            },
        });

        b.installArtifact(test_pbtools);

        // test_runtime
        const test_runtime = b.addExecutable(.{
            .name = "test_runtime",
            .root_module = b.createModule(.{
                .target = target,
                .optimize = optimize,
            }),
        });

        test_runtime.linkLibrary(lib);
        if (b.lazyDependency("libsodium", .{
            .target = target,
            .optimize = optimize,
            .static = true,
            .shared = false,
        })) |dep| {
            test_runtime.linkLibrary(dep.artifact(if (target.result.os.tag == .windows) "libsodium-static" else "sodium"));
        }
        if (googletest_dep) |dep| {
            test_runtime.linkLibrary(dep.artifact("gtest"));
            test_runtime.linkLibrary(dep.artifact("gtest_main"));
        }

        test_runtime.addIncludePath(b.path("include"));
        test_runtime.addIncludePath(b.path("third_party"));

        test_runtime.addCSourceFiles(.{
            .files = &.{
                "tests/unit/src/test_runtime.cpp",
                "tests/unit/src/key_pairs.cpp",
            },
        });

        b.installArtifact(test_runtime);
    }
}
