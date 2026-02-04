#!/bin/bash
# Generate ZLE thingies.list, widgets.list, zle_things.h, and zle_widget.h

set -e

IWIDGETS_LIST="$1"
OUTPUT_DIR="$2"
ZLE_THINGS_SED="$3"
ZLE_WIDGET_SED="$4"

mkdir -p "$OUTPUT_DIR"

# Generate thingies.list
{
    echo '/** thingies.list                            **/'
    echo '/** thingy structures for the known thingies **/'
    echo ''
    echo '/* format: T("name", TH_FLAGS, w_widget, t_nextthingy) */'
    echo ''
    sed -e 's/#.*//; /^$/d; s/" *,.*/"/' \
        -e 's/^"/T("/; s/$/, 0,/; h' \
        -e 's/-//g; s/^.*"\(.*\)".*/w_\1, t_D\1)/' \
        -e 'H; g; s/\n/ /' \
        < "$IWIDGETS_LIST"
    sed -e 's/#.*//; /^$/d; s/" *,.*/"/' \
        -e 's/^"/T("./; s/$/, TH_IMMORTAL,/; h' \
        -e 's/-//g; s/^.*"\.\(.*\)".*/w_\1, t_\1)/' \
        -e 'H; g; s/\n/ /' \
        < "$IWIDGETS_LIST"
} > "$OUTPUT_DIR/thingies.list"

# Generate widgets.list
{
    echo '/** widgets.list                               **/'
    echo '/** widget structures for the internal widgets **/'
    echo ''
    echo '/* format: W(ZLE_FLAGS, t_firstname, functionname) */'
    echo ''
    sed -e 's/#.*//; /^$/d; s/-//g' \
        -e 's/^"\(.*\)" *, *\([^ ]*\) *, *\(.*\)/W(\3, t_\1, \2)/' \
        < "$IWIDGETS_LIST"
} > "$OUTPUT_DIR/widgets.list"

# Generate zle_things.h
{
    echo '/** zle_things.h                              **/'
    echo '/** indices of and pointers to known thingies **/'
    echo ''
    echo 'enum {'
    sed -n -f "$ZLE_THINGS_SED" < "$OUTPUT_DIR/thingies.list"
    echo '    ZLE_BUILTIN_THINGY_COUNT'
    echo '};'
} > "$OUTPUT_DIR/zle_things.h"

# Generate zle_widget.h
{
    echo '/** zle_widget.h                                **/'
    echo '/** indices of and pointers to internal widgets **/'
    echo ''
    echo 'enum {'
    sed -n -f "$ZLE_WIDGET_SED" < "$OUTPUT_DIR/widgets.list"
    echo '    ZLE_BUILTIN_WIDGET_COUNT'
    echo '};'
} > "$OUTPUT_DIR/zle_widget.h"

echo "Generated ZLE headers in $OUTPUT_DIR"
