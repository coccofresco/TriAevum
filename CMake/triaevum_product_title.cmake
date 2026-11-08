# The product title a configuration builds (G4a-F5): its executable icon and
# product identity. TRIAEVUM_PRODUCT_TITLE names it and TRIAEVUM_TITLES must
# contain that title's adapter: an oot3d product needs oot3d, an mm3d product
# needs mm3d. The Moonfall Curse release configure passes
# -DTRIAEVUM_PRODUCT_TITLE=mm3d (docs/MOONFALL_CURSE.md "Release configure").
function(triaevum_check_product_title product titles)
    if(NOT product MATCHES "^(oot3d|mm3d)$")
        message(FATAL_ERROR "TRIAEVUM_PRODUCT_TITLE: unknown title '${product}' (oot3d, mm3d)")
    endif()
    if(NOT product IN_LIST titles)
        message(FATAL_ERROR
            "TRIAEVUM_PRODUCT_TITLE=${product} needs ${product} in TRIAEVUM_TITLES (got '${titles}')")
    endif()
endfunction()
