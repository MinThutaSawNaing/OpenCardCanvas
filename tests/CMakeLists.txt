# ---------------------------------------------------------------------------
# OpenCardCanvas unit / integration tests (Qt Test)
#
# Tests run headless through QT_QPA_PLATFORM=offscreen so they work on a build
# agent with no desktop session.
# ---------------------------------------------------------------------------
set(OCC_TEST_LIBS occcore Qt6::Test Qt6::Core Qt6::Gui Qt6::Widgets)

function(occ_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ${OCC_TEST_LIBS})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

# --- Code generators -------------------------------------------------------
# The QR encoder is checked module-for-module against golden matrices produced
# by an INDEPENDENT reference implementation (segno). Regenerate with:
#     python tools/qr/generate_golden.py
occ_add_test(test_qr)
set_tests_properties(test_qr PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;OCC_QR_GOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/data/qr_golden.json")

occ_add_test(test_barcode)       # Code128 / Code39 / EAN-13 / EAN-8 / ITF-14 + validation

# --- Foundations -----------------------------------------------------------
occ_add_test(test_units)         # unit conversion and physical accuracy
occ_add_test(test_zip)           # .occard container, CRC-32, path traversal safety
occ_add_test(test_objects)       # object model, transforms, hit testing, z-order
occ_add_test(test_undo)          # undo / redo coverage of editor operations
occ_add_test(test_serialization) # project save / load / compare / version handling
occ_add_test(test_rendering)     # objects actually render to pixels
occ_add_test(test_template)      # {{placeholder}} expansion
occ_add_test(test_csv)           # CSV import: valid and malformed input
occ_add_test(test_canvas)        # snapping, guides and view/geometry maths
occ_add_test(test_printer)       # printer abstraction and the simulator backend
occ_add_test(test_errors)        # invalid projects, missing files, bad input
occ_add_test(test_layout)        # dock containment and scrolling at compact window sizes
occ_add_test(test_layer_ui)      # resize cursor directions and layer input/undo
occ_add_test(test_property_colors) # modal colour picking, cancellation and undo
occ_add_test(test_printer_ui)    # saved printer selection and preview
