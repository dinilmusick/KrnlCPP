"""Reproducible Conan 2 recipe for KrnlCPP; version is canonical version.txt."""
from pathlib import Path

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class UltrstNativePackage(ConanFile):
    name = "krnlcpp"
    description = "Portable native ULTRST library from dinilmusick/KrnlCPP"
    package_type = "header-library"
    settings = "os", "arch", "compiler", "build_type"
    exports = "version.txt"
    exports_sources = "CMakeLists.txt", "version.txt", "src/*", "include/*", "cmake/*", "LICENSE*", "COPYING*", "README.md"
    generators = ()

    def set_version(self):
        self.version = Path(self.recipe_folder, "version.txt").read_text().strip()

    def requirements(self):
        pass

    def layout(self):
        cmake_layout(self)

    def generate(self):
        CMakeToolchain(self).generate()
        CMakeDeps(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure(variables={"BUILD_TESTING": "OFF", "KLOKSC_BUILD_TESTS": "OFF"})
        cmake.build()

    def package(self):
        CMake(self).install()

    def package_id(self):
        self.info.clear()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "KrnlCpp")
        self.cpp_info.set_property("cmake_target_name", "krnl::krnlcpp")
        self.cpp_info.libs = []
        if str(self.settings.os) == "Linux" and self.cpp_info.libs and self.name in ("gutc", "kloksc", "floksc"):
            self.cpp_info.system_libs = ["m"]
