if(NOT DEFINED MICROPANEL_TOUCH_BINARY OR NOT DEFINED LEGACY_CONFIG_ROOT)
    message(FATAL_ERROR "MICROPANEL_TOUCH_BINARY and LEGACY_CONFIG_ROOT are required")
endif()

file(GLOB fixtures "${LEGACY_CONFIG_ROOT}/*.json")
list(SORT fixtures)
list(LENGTH fixtures fixture_count)
if(NOT fixture_count EQUAL 14)
    message(FATAL_ERROR "Expected 14 legacy fixtures, found ${fixture_count}")
endif()

foreach(fixture IN LISTS fixtures)
    execute_process(
        COMMAND "${MICROPANEL_TOUCH_BINARY}" --validate-config "${fixture}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Validation failed for ${fixture}: ${error}")
    endif()
    if(NOT output MATCHES "module_declarations=[0-9]+" OR
       NOT output MATCHES "submenu_references=[0-9]+")
        message(FATAL_ERROR "Validation did not report counts for ${fixture}: ${output}")
    endif()
endforeach()

set(config_pios_new "${LEGACY_CONFIG_ROOT}/config-pios-new.json")
execute_process(
    COMMAND "${MICROPANEL_TOUCH_BINARY}" --validate-config "${config_pios_new}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT output MATCHES "module_declarations=55" OR
   NOT output MATCHES "submenu_references=59")
    message(FATAL_ERROR "config-pios-new parity counts are wrong: ${output}${error}")
endif()
