# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED PRIMEFORGE_ROOT)
    message(FATAL_ERROR "PRIMEFORGE_ROOT is required")
endif()

if(NOT DEFINED MANIFEST)
    message(FATAL_ERROR "MANIFEST is required")
endif()

if(NOT EXISTS "${MANIFEST}")
    message(FATAL_ERROR "Distribution manifest is missing: ${MANIFEST}")
endif()

file(STRINGS "${MANIFEST}" manifest_lines ENCODING UTF-8)
string(ASCII 9 tab)
set(component_count 0)
set(redistributed_count 0)
set(original_count 0)

foreach(line IN LISTS manifest_lines)
    if(line STREQUAL "" OR line MATCHES "^#")
        continue()
    endif()
    if(line MATCHES "^component${tab}")
        continue()
    endif()

    string(REPLACE "${tab}" ";" fields "${line}")
    list(LENGTH fields field_count)
    if(NOT field_count EQUAL 8)
        message(FATAL_ERROR "Malformed distribution manifest row (${field_count} fields): ${line}")
    endif()

    list(GET fields 0 component)
    list(GET fields 1 classification)
    list(GET fields 2 integration)
    list(GET fields 3 redistributed)
    list(GET fields 4 license_file)
    list(GET fields 5 expected_sha256)
    list(GET fields 6 notice_file)
    list(GET fields 7 source_id)

    if(NOT classification MATCHES "^(ORIGINAL|LINKED|EXTERNAL|REFERENCE|REJECTED|CI_ONLY|DEVELOPMENT_TOOL)$")
        message(FATAL_ERROR "Invalid classification for ${component}: ${classification}")
    endif()
    if(NOT redistributed MATCHES "^(YES|NO)$")
        message(FATAL_ERROR "Invalid redistributed value for ${component}: ${redistributed}")
    endif()
    if(source_id STREQUAL "")
        message(FATAL_ERROR "Missing source identifier for ${component}")
    endif()

    math(EXPR component_count "${component_count} + 1")
    if(classification STREQUAL "ORIGINAL")
        math(EXPR original_count "${original_count} + 1")
    endif()

    if(redistributed STREQUAL "YES")
        math(EXPR redistributed_count "${redistributed_count} + 1")
        if(license_file STREQUAL "NOT_APPLICABLE" OR expected_sha256 STREQUAL "UNKNOWN")
            message(FATAL_ERROR "Redistributed component ${component} lacks a pinned license")
        endif()

        set(license_path "${PRIMEFORGE_ROOT}/${license_file}")
        if(NOT EXISTS "${license_path}")
            message(FATAL_ERROR "Required license file is missing for ${component}: ${license_path}")
        endif()
        file(SIZE "${license_path}" license_size)
        if(license_size EQUAL 0)
            message(FATAL_ERROR "Required license file is empty for ${component}: ${license_path}")
        endif()
        file(SHA256 "${license_path}" actual_sha256)
        string(TOUPPER "${actual_sha256}" actual_sha256)
        string(TOUPPER "${expected_sha256}" expected_sha256)
        if(NOT actual_sha256 STREQUAL expected_sha256)
            message(FATAL_ERROR
                "License hash mismatch for ${component}: expected ${expected_sha256}, got ${actual_sha256}"
            )
        endif()

        if(NOT notice_file STREQUAL "NOT_REQUIRED")
            set(notice_path "${PRIMEFORGE_ROOT}/${notice_file}")
            if(NOT EXISTS "${notice_path}")
                message(FATAL_ERROR "Required notice file is missing for ${component}: ${notice_path}")
            endif()
        endif()
    else()
        if(NOT license_file STREQUAL "NOT_APPLICABLE")
            message(FATAL_ERROR
                "Non-redistributed component ${component} must use NOT_APPLICABLE as distribution license path"
            )
        endif()
    endif()
endforeach()

if(component_count EQUAL 0)
    message(FATAL_ERROR "Distribution manifest contains no components")
endif()
if(original_count EQUAL 0)
    message(FATAL_ERROR "Distribution manifest does not identify original PrimeForge code")
endif()

message(STATUS
    "Distribution license verification PASS: ${component_count} components, ${redistributed_count} redistributed"
)
