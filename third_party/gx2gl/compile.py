import os
import sys
import subprocess
import multiprocessing
import shutil
# this script is just for convenience, you can invoke make manually if need be
def get_env_var(name, default=None):
    val = os.environ.get(name)
    if not val:
        return default
    return val

def main():
    print("Starting Universal Build...")


    devkitpro = get_env_var("DEVKITPRO")
    

    if os.name == "nt" and devkitpro == "/opt/devkitpro":
        devkitpro = "C:/devkitPro"

    if not devkitpro:
        default_linux = "/opt/devkitpro"
        default_win = "C:/devkitPro"
        if os.path.isdir(default_linux):
            devkitpro = default_linux
        elif os.path.isdir(default_win):
            devkitpro = default_win
        else:
            print("Error: DEVKITPRO environment variable is not set and default locations do not exist.")
            sys.exit(1)
    
    os.environ["DEVKITPRO"] = devkitpro

    devkitppc = get_env_var("DEVKITPPC")
    if not devkitppc:
        devkitppc = os.path.join(devkitpro, "devkitPPC")
        os.environ["DEVKITPPC"] = devkitppc

    tools_bin = os.path.join(devkitpro, "tools", "bin").replace("\\", "/")
    

    paths_to_add = [os.path.join(devkitppc, "bin"), tools_bin]
    current_path = os.environ.get("PATH", "")
    for p in paths_to_add:
        if p not in current_path:
            current_path = p + os.pathsep + current_path
    os.environ["PATH"] = current_path

    build_dir = "build"
    generator = "Unix Makefiles"
    if shutil.which("ninja"):
        generator = "Ninja"

    cache_path = os.path.join(build_dir, "CMakeCache.txt")
    if os.path.isfile(cache_path):
        with open(cache_path, "r", encoding="utf-8", errors="ignore") as cache_file:
            cache_contents = cache_file.read()
        marker = "CMAKE_GENERATOR:INTERNAL="
        cache_generator = None
        for line in cache_contents.splitlines():
            if line.startswith(marker):
                cache_generator = line[len(marker):]
                break
        if cache_generator and cache_generator != generator:
            shutil.rmtree(build_dir)

    if not os.path.exists(build_dir):
        os.makedirs(build_dir)
    

    print("Configuring with CMake...")
    toolchain_file = os.path.join(devkitpro, "cmake", "WiiU.cmake").replace("\\", "/")
    cmake_cmd = [
        "cmake", "-S", ".", "-B", build_dir,
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file}",
        "-G", generator
    ]

    result = subprocess.run(cmake_cmd)
    if result.returncode != 0:
        print("CMake configuration FAILED.")
        sys.exit(1)

    # Compile
    print("Compiling...")
    cores = str(multiprocessing.cpu_count())
    build_cmd = ["cmake", "--build", build_dir, "--parallel", cores, "--target", "gl33_test"]
    result = subprocess.run(build_cmd)
    if result.returncode != 0:
        print("Build FAILED during compilation.")
        sys.exit(1)

    # wut_create_rpx performs the ELF-to-RPX conversion as a post-build step.
    rpx_file = "gl33_test.rpx"
    if not os.path.isfile(os.path.join(build_dir, rpx_file)):
        print(f"Build completed, but {rpx_file} was not produced.")
        sys.exit(1)

    print("-" * 48)
    print("Build Successful!")
    print(f"Output located in: {build_dir}/{rpx_file}")
    print("-" * 48)

if __name__ == "__main__":
    main()
