# 发布版本（四段式）。只修改这里的默认值；本地构建仍可用 -DVERSION_BUILD=... 覆盖。
# Keep each default as one literal set() line: CI reads these without running CMake.
if(NOT DEFINED VERSION_MAJOR)
    set(VERSION_MAJOR 0)
endif()
if(NOT DEFINED VERSION_MINOR)
    set(VERSION_MINOR 1)
endif()
if(NOT DEFINED VERSION_PATCH)
    set(VERSION_PATCH 0)
endif()
if(NOT DEFINED VERSION_BUILD)
    set(VERSION_BUILD 0)
endif()

foreach(component MAJOR MINOR PATCH BUILD)
    set(value "${VERSION_${component}}")
    string(LENGTH "${value}" value_length)
    if(NOT value MATCHES "^(0|[1-9][0-9]*)$" OR value_length GREATER 5 OR value GREATER 65535)
        message(FATAL_ERROR "VERSION_${component} must be a canonical integer between 0 and 65535; got '${value}'")
    endif()
endforeach()
unset(value)
unset(value_length)
unset(component)

set(APP_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}.${VERSION_BUILD}")
set(C_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
set(F_VERSION "${VERSION_BUILD}")

# Apple permits at most three CFBundleVersion integers. Pack PATCH and BUILD
# into one integer so build-only changes remain distinct and retain ordering.
# https://developer.apple.com/documentation/bundleresources/information-property-list/cfbundleversion
math(EXPR CHUNFE_MACOS_BUILD_COMPONENT "${VERSION_PATCH} * 65536 + ${VERSION_BUILD}")
set(CHUNFE_MACOS_BUNDLE_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${CHUNFE_MACOS_BUILD_COMPONENT}")
