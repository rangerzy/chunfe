"""Version contracts exercised without Qt or a native package toolchain."""

import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
READER = ROOT / "scripts/ci/app_version.py"


def version_source(major="0", minor="1", patch="0", build="0"):
    return "\n".join(
        f"if(NOT DEFINED VERSION_{name})\n    set(VERSION_{name} {value})\nendif()"
        for name, value in zip(("MAJOR", "MINOR", "PATCH", "BUILD"), (major, minor, patch, build))
    ) + "\n"


class AppVersionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if READER.is_file():
            spec = importlib.util.spec_from_file_location("app_version", READER)
            cls.version = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(cls.version)

    def setUp(self):
        self.assertTrue(READER.is_file(), "The dependency-free version reader must exist")

    def test_parses_canonical_four_components_and_windows_upper_bound(self):
        self.assertEqual(self.version.parse_version("0.1.0.0"), (0, 1, 0, 0))
        self.assertEqual(self.version.parse_version("65535.65535.65535.65535"), (65535,) * 4)

    def test_rejects_noncanonical_or_unrepresentable_versions(self):
        for value in ("", "1.2.3", "1.2.3.4.5", "v1.2.3.4", "01.2.3.4", "1.02.3.4", "1.2.3.04", "1.2.-3.4", "1.2.3.4\n", " 1.2.3.4", "1.2.3.4-rc1", "1.2.3.65536", "65536.0.0.0", "1.2.3.１２", "1.2.3.99999999999999999999999999999"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.version.parse_version(value)

    def test_legacy_requires_explicit_opt_in_and_keeps_three_components(self):
        self.assertEqual(self.version.parse_version("0.1.0", allow_legacy=True), (0, 1, 0))
        self.assertEqual(self.version.parse_version("0.1.0.0", allow_legacy=True), (0, 1, 0, 0))
        for value in ("0.1", "0.01.0", "0.1.65536"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.version.parse_version(value, allow_legacy=True)

    def test_reads_literal_defaults_without_executing_cmake(self):
        source = "# set(VERSION_MAJOR 99) is only a comment\n" + version_source("2", "3", "4", "5")
        self.assertEqual(self.version.version_from_source(source), "2.3.4.5")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "cmake").mkdir()
            (root / "cmake/AppVersion.cmake").write_text(source)
            self.assertEqual(self.version.read_version(root), "2.3.4.5")

    def test_rejects_missing_duplicate_or_nonliteral_declarations(self):
        valid = version_source()
        for source in (
            valid.replace("    set(VERSION_BUILD 0)\n", ""),
            valid + "set(VERSION_BUILD 1)\n",
            valid + "set(VERSION_BUILD 0)\n",
            valid.replace("set(VERSION_BUILD 0)", "set(VERSION_BUILD ${BUILD})"),
            valid.replace("set(VERSION_BUILD 0)", "set(VERSION_BUILD 00)"),
            valid.replace("set(VERSION_BUILD 0)", "set(VERSION_BUILD 65536)"),
            valid.replace("set(VERSION_BUILD 0)", "set(VERSION_BUILD 0 CACHE STRING version)"),
        ):
            with self.subTest(source=source), self.assertRaises(ValueError):
                self.version.version_from_source(source)

    def run_cli(self, *args):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "scripts/ci").mkdir(parents=True)
            (root / "cmake").mkdir()
            reader = root / "scripts/ci/app_version.py"
            shutil.copyfile(READER, reader)
            (root / "cmake/AppVersion.cmake").write_text(version_source())
            return subprocess.run([sys.executable, str(reader), *args], cwd=tempfile.gettempdir(), text=True, capture_output=True)

    def test_cli_prints_version_from_its_repository_outside_cwd(self):
        result = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "0.1.0.0\n")

    def test_cli_checks_tag_matches_declared_source(self):
        for tag, success in (("v0.1.0.0", True), ("v0.1.0.1", False), ("0.1.0.0", False), ("v0.1.0", False), ("v00.1.0.0", False)):
            with self.subTest(tag=tag):
                result = self.run_cli("--check-tag", tag)
                self.assertEqual(result.returncode == 0, success, result.stderr)
                if success:
                    self.assertEqual(result.stdout, "0.1.0.0\n")
                else:
                    self.assertEqual(result.stdout, "")


@unittest.skipUnless(shutil.which("cmake"), "CMake is required")
class CMakeVersionTests(unittest.TestCase):
    def run_version(self, **overrides):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "version.cmake"
            script.write_text(
                f'include("{ROOT / "cmake/AppVersion.cmake"}")\n'
                'message(STATUS "VERSIONS=${APP_VERSION}|${C_VERSION}|${F_VERSION}|${CHUNFE_MACOS_BUNDLE_VERSION}")\n'
            )
            return subprocess.run(["cmake", *[f"-D{key}={value}" for key, value in overrides.items()], "-P", str(script)], text=True, capture_output=True)

    def test_cmake_default_preserves_the_four_part_application_version(self):
        result = self.run_version()
        self.assertEqual(result.returncode, 0, result.stderr)
        # CMake's evaluated default must agree with the independent CI reader.
        declared = subprocess.check_output([sys.executable, str(READER)], text=True).strip()
        self.assertIn(f"VERSIONS={declared}|", result.stdout)

    def test_cmake_overrides_map_all_components_to_apple_build_version(self):
        result = self.run_version(VERSION_MAJOR="2", VERSION_MINOR="3", VERSION_PATCH="4", VERSION_BUILD="5")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("VERSIONS=2.3.4.5|2.3.4|5|2.3.262149", result.stdout)
        maximum = self.run_version(VERSION_MAJOR="65535", VERSION_MINOR="65535", VERSION_PATCH="65535", VERSION_BUILD="65535")
        self.assertEqual(maximum.returncode, 0, maximum.stderr)
        self.assertIn("65535.65535.4294967295", maximum.stdout)

    def test_cmake_rejects_invalid_override_before_project_configuration(self):
        for value in ("", "-1", "01", "65536", "1.0", "1;2", "999999999999999999999999"):
            with self.subTest(value=value):
                result = self.run_version(VERSION_BUILD=value)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("VERSION_BUILD", result.stderr)


    def test_application_target_exports_full_version_and_three_part_apple_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            (source / "CMakeLists.txt").write_text(f"""
cmake_minimum_required(VERSION 3.24)
set(VERSION_MAJOR 2)
set(VERSION_MINOR 3)
set(VERSION_PATCH 4)
set(VERSION_BUILD 5)
include("{ROOT / 'cmake/AppVersion.cmake'}")
project(VersionProbe VERSION ${{APP_VERSION}} LANGUAGES CXX)
set(CMAKE_INSTALL_BINDIR bin)
set(CMAKE_INSTALL_DATADIR share)
function(qt_add_executable name)
    add_executable(${{name}} EXCLUDE_FROM_ALL ${{ARGN}})
endfunction()
function(qt_generate_deploy_qml_app_script)
    file(WRITE "${{CMAKE_BINARY_DIR}}/deploy.cmake" "")
    set(deploy_script "${{CMAKE_BINARY_DIR}}/deploy.cmake" PARENT_SCOPE)
endfunction()
add_library(chunfe_gui INTERFACE)
set_property(TARGET chunfe_gui PROPERTY QT_QML_MODULE_QML_FILES "")
foreach(name Qt6::Quick Qt6::QuickControls2 chunfe_fonts chunfe_guiplugin)
    add_library(${{name}} INTERFACE IMPORTED)
endforeach()
add_subdirectory("{ROOT / 'src/app'}" app)
file(GENERATE OUTPUT "${{CMAKE_BINARY_DIR}}/target-version.txt" CONTENT "$<TARGET_PROPERTY:Chunfe,MACOSX_BUNDLE_SHORT_VERSION_STRING>|$<TARGET_PROPERTY:Chunfe,MACOSX_BUNDLE_BUNDLE_VERSION>|$<TARGET_PROPERTY:Chunfe,COMPILE_DEFINITIONS>")
""")
            result = subprocess.run(["cmake", "-S", str(source), "-B", str(source / "build")], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            properties = (source / "build/target-version.txt").read_text()
            self.assertTrue(properties.startswith("2.3.4|2.3.262149|"), properties)
            self.assertIn('CHUNFE_VERSION="2.3.4.5"', properties)


    def test_linux_installs_compiled_version_under_share_chunfe(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            (source / "probe.cpp").write_text("int main() { return 0; }\n")
            (source / "CMakeLists.txt").write_text(f"""
cmake_minimum_required(VERSION 3.24)
project(VersionInstallProbe VERSION 2.3.4.5 LANGUAGES CXX)
# Exercise the Linux installation branch even on a macOS development host.
set(APPLE FALSE)
set(UNIX TRUE)
set(PROJECT_SOURCE_DIR "{ROOT}")
set(CMAKE_INSTALL_BINDIR bin)
set(CMAKE_INSTALL_DATADIR share)
function(qt_add_executable name)
    add_executable(${{name}} "${{CMAKE_CURRENT_FUNCTION_LIST_DIR}}/probe.cpp")
endfunction()
add_library(chunfe_gui INTERFACE)
set_property(TARGET chunfe_gui PROPERTY QT_QML_MODULE_QML_FILES "")
foreach(name Qt6::Quick Qt6::QuickControls2 chunfe_fonts chunfe_guiplugin)
    add_library(${{name}} INTERFACE IMPORTED)
endforeach()
add_subdirectory("{ROOT / 'src/app'}" app)
set_target_properties(Chunfe PROPERTIES MACOSX_BUNDLE FALSE)
file(GENERATE OUTPUT "${{CMAKE_BINARY_DIR}}/version.txt" CONTENT "${{PROJECT_VERSION}}\\n")
""")
            build = source / "build"
            installed = source / "installed"
            for command in (["cmake", "-S", str(source), "-B", str(build)], ["cmake", "--build", str(build)], ["cmake", "--install", str(build), "--prefix", str(installed)]):
                result = subprocess.run(command, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
            version_file = installed / "share/chunfe/version.txt"
            self.assertTrue(version_file.is_file(), "Linux packages must carry their CMake-generated application version")
            self.assertEqual(version_file.read_bytes(), b"2.3.4.5\n")


class PackageVersionTests(unittest.TestCase):
    def run_macos_package(self, version):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "packaging/macos").mkdir(parents=True)
            (root / "build").mkdir()
            (root / "bin").mkdir()
            shutil.copyfile(ROOT / "packaging/macos/package.sh", root / "packaging/macos/package.sh")
            (root / "build/version.txt").write_text(version + "\n")
            # Installation is the first native-tool boundary after validation.
            cmake = root / "bin/cmake"
            cmake.write_text("#!/bin/sh\nexit 77\n")
            cmake.chmod(0o755)
            return subprocess.run(["bash", str(root / "packaging/macos/package.sh"), str(root / "build")], env={**os.environ, "PATH": f"{root / 'bin'}:{os.environ['PATH']}"}, text=True, capture_output=True)

    def test_macos_accepts_four_part_version_at_install_boundary(self):
        result = self.run_macos_package("2.3.4.5")
        self.assertEqual(result.returncode, 77, result.stderr)

    def test_macos_rejects_noncanonical_or_out_of_range_version_before_install(self):
        for value in ("1.2.3", "01.2.3.4", "1.2.3.65536", "1.2.3.4\n5"):
            with self.subTest(value=value):
                result = self.run_macos_package(value)
                self.assertNotEqual(result.returncode, 77, result.stderr)
                self.assertIn("Invalid application version", result.stderr)


    def run_flatpak_package(self, source, *, built_version=None, installed_version=None):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "scripts/ci").mkdir(parents=True)
            (root / "cmake").mkdir()
            (root / "bin").mkdir()
            shutil.copyfile(ROOT / "scripts/ci/build-flatpak.sh", root / "scripts/ci/build-flatpak.sh")
            shutil.copyfile(READER, root / "scripts/ci/app_version.py")
            (root / "CMakeLists.txt").write_text('include(cmake/AppVersion.cmake)\nproject(Chunfe VERSION ${APP_VERSION} LANGUAGES CXX)\n')
            (root / "cmake/AppVersion.cmake").write_text(source)
            # Only package-manager tools are substituted; all validation is real.
            commands = {
                "uname": '#!/bin/sh\ncase "$1" in -s) echo Linux;; -m) echo x86_64;; esac\n',
                "flatpak": "#!/bin/sh\nexit 77\n",
                **{name: "#!/bin/sh\nexit 0\n" for name in ("flatpak-builder", "desktop-file-validate", "eu-strip", "eu-elfcompress")},
            }
            if built_version is not None:
                commands["flatpak-builder"] = f'''#!{sys.executable}
from pathlib import Path
root = Path("build/flatpak/files")
for relative in ("bin/Chunfe", "share/applications/io.github.rangerzy.chunfe.desktop", "share/icons/hicolor/scalable/apps/io.github.rangerzy.chunfe.svg", "share/chunfe/version.txt"):
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("payload")
(root / "bin/Chunfe").chmod(0o755)
(root / "share/chunfe/version.txt").write_text({built_version!r})
'''
                commands["flatpak"] = f'''#!{sys.executable}
from pathlib import Path
import shutil
import sys
command = sys.argv[1]
installed_version = {installed_version!r}
if command == "run":
    print("6.12.0")
elif command == "build-bundle":
    if installed_version is None:
        raise SystemExit(77)
    Path("dist/chunfe-2.3.4.5-linux-x64.flatpak").write_text("bundle")
elif command == "install" and "--reinstall" in sys.argv:
    shutil.copytree("build/flatpak", "installed")
    Path("installed/metadata").write_text("[Application]\\nname=io.github.rangerzy.chunfe\\ncommand=Chunfe\\nruntime=org.kde.Platform/x86_64/6.12-beta\\n")
    Path("installed/files/share/chunfe/version.txt").write_text(installed_version)
elif command == "info":
    print("app/io.github.rangerzy.chunfe/x86_64/stable" if "--show-ref" in sys.argv else Path("installed").resolve())
'''
            for name, contents in commands.items():
                command = root / "bin" / name
                command.write_text(contents)
                command.chmod(0o755)
            return subprocess.run(["bash", str(root / "scripts/ci/build-flatpak.sh")], env={**os.environ, "PATH": f"{root / 'bin'}:{os.environ['PATH']}"}, text=True, capture_output=True)

    def test_flatpak_reads_shared_four_part_source_before_native_build(self):
        result = self.run_flatpak_package(version_source("2", "3", "4", "5"))
        self.assertEqual(result.returncode, 77, result.stderr)

    def test_flatpak_rejects_invalid_source_before_package_manager_operation(self):
        result = self.run_flatpak_package(version_source(build="65536"))
        self.assertNotEqual(result.returncode, 77, result.stderr)
        self.assertIn("Invalid application version", result.stderr)


    def test_flatpak_matching_compiled_version_reaches_bundle_export(self):
        result = self.run_flatpak_package(version_source("2", "3", "4", "5"), built_version="2.3.4.5\n")
        self.assertEqual(result.returncode, 77, result.stderr)

    def test_flatpak_mismatched_compiled_version_fails_before_bundle_export(self):
        for built_version in ("2.3.4.6\n", "2.3.4.5\n\n", "2.3.4.5", ""):
            with self.subTest(built_version=built_version):
                result = self.run_flatpak_package(version_source("2", "3", "4", "5"), built_version=built_version)
                self.assertNotEqual(result.returncode, 77, result.stderr)
                self.assertIn("Built Flatpak version does not match declared version 2.3.4.5", result.stderr)

    def test_flatpak_matching_installed_version_passes_payload_validation(self):
        result = self.run_flatpak_package(version_source("2", "3", "4", "5"), built_version="2.3.4.5\n", installed_version="2.3.4.5\n")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_flatpak_changed_installed_version_fails_payload_validation(self):
        result = self.run_flatpak_package(version_source("2", "3", "4", "5"), built_version="2.3.4.5\n", installed_version="2.3.4.6\n")
        self.assertNotEqual(result.returncode, 0, result.stderr)
        self.assertIn("Installed Flatpak payload differs: share/chunfe/version.txt", result.stderr)


if __name__ == "__main__":
    unittest.main()
