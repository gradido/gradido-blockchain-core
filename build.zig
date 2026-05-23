const std = @import("std");

/// Recursively add .c files from a directory
fn addDirSources(
    lib: *std.Build.Step.Compile,
    b: *std.Build,
    dirs: []const []const u8,
) void {
    for (dirs) |dir| {
        var it = std.fs.cwd().openDir(dir, .{ .iterate = true }) catch continue;
        defer it.close();

        var walker = it.walk(b.allocator) catch continue;
        defer walker.deinit();

        while (walker.next() catch null) |entry| {
            if (entry.kind == .file and std.mem.endsWith(u8, entry.path, ".c")) {
                const full_path = b.fmt("{s}/{s}", .{ dir, entry.path });

                lib.addCSourceFiles(.{
                    .files = &[_][]const u8{full_path},
                    .flags = &.{},
                });
            }
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
    lib.addIncludePath(b.path("third_party"));

    const data_sources = [_][]const u8{
           "src/data",
       };

       const utils_sources = [_][]const u8{
           "src/utils",
       };

       const third_party_sources = [_][]const u8{
           "third_party/fp256/src",
           "third_party/r128",
       };

    addDirSources(lib, b, &data_sources);
    addDirSources(lib, b, &utils_sources);
    addDirSources(lib, b, &third_party_sources);

    b.installArtifact(lib);

    if (enable_benchmarks) {
        const bench = b.addExecutable(.{
            .name = "bench_numberToString",
            .root_module = b.createModule(.{
                .target = target,
                .optimize = optimize,
            })
        });

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
            src:  []const u8,
        }{
            .{ .name = "test_converter", .src = "tests/unit/src/test_converter.cpp" },
            .{ .name = "test_duration",  .src = "tests/unit/src/test_duration.cpp"  },
            .{ .name = "test_unit",      .src = "tests/unit/src/test_unit.cpp"      },
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
    }
}
