#!/bin/bash -x

touch $HOME/libinote.ok
rm -f /tmp/libinote.log.*

#unset QUIET
QUIET=1

unset WITH_GDB
if [ "$1" = "-g" ]; then
	WITH_GDB=1
fi

INOTE_INCOMPLETE_MULTIBYTE=4

testFileUrl="http://abu.cnam.fr/cgi-bin/donner_unformated?nddp1"
file1_orig=nddp1.orig.txt
file8_orig=nddp8${file1_orig#nddp1}
file1=${file1_orig%.orig.txt}.10k.txt
file8=${file8_orig%.orig.txt}.10k.txt
if [ ! -e "$file8" ]; then
	wget -O - "$testFileUrl" | sed -e 's/[\r]//g' -e "/^$/d" > "$file1_orig"
	iconv -f iso-8859-1 -t utf-8 -o "$file8_orig" "$file1_orig"  
	dd if=$file1_orig of=$file1 bs=1024 count=10
	iconv -f iso-8859-1 -t utf-8 -o "$file8" "$file1"  
fi



testSentence() {
    T125="ééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééé"
    T126="éééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééé"
    T127="ééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééé"
    T256=${T127}${T127}éé

    #unset testArray
    i=0

    # ---> 
    echo "$i. BEGIN +CAPS -BEEP +PUNCT"
    VERSION_COMPAT=-1
    CAPS_MODE=1
    unset CAPS_PREFIX
    PUNCT_MODE=1

    # the order of tests is important (index used to named the corresponding report)
    label="utf-8 text"
    text="   Un éléphant"
    convertFile $((i++))

    label="utf-8 text"
    text=",   Un éléphant"
    convertFile $((i++))

    label="utf-8 text + filtered annotation + tag + punctuation"
    text="  \`gfa1 \`gfa2 \`Pf2()? <speak>Un &lt;éléphant&gt; (1)</speak>"
    convertFile $((i++))

    label="utf-8 text + annotation"
    text=" \`v1 Un \`v2 éléphant"
    convertFile $((i++))

    label="1 tlv for 127 é (header=2 bytes + value=254 bytes)"
    text=${T127}
    convertFile $((i++))

    label="2 tlv: 127 é + a"
    text=${T127}a
    convertFile $((i++))

    label="1 tlv: a + 125 é"
    text=a${T125}
    convertFile $((i++))

    label="2 tlv, last utf-8 splitted: a + 125 é + 4 bytes utf8 char 𪚥"
    text=a${T125}𪚥
    convertFile $((i++))

    label="1025 bytes: a + 512 é"
    text=$(echo -n "a$T256$T256")
    convertFile $((i++))

    label="6 bytes: é + 2 erroneous utf-8 bytes + é"
    text=$(echo -en "é\xca\xfeé")
    convertFile $((i++))

    label="256 bytes: 127 é + 2 erroneous bytes"
    text=$(echo -en "${T127}\xca\xfe")
    convertFile $((i++))

    label="257 bytes: a + 127 é + 2 erroneous bytes"
    text=$(echo -en "a${T127}\xca\xfe")
    convertFile $((i++))

    label="utf-8 text + language switching annotation"
    text="Un éléphant \`l0x12345678 One elephant"
    convertFile $((i++))

    # ---> 
    echo "$i. BEGIN +CAPS +BEEP +PUNCT"
    # default version
    VERSION_COMPAT=-1

    # enable TLV for capitalized word
    CAPS_MODE=1

    # prefix each capitalized word in the resulting text
    CAPS_PREFIX="beep"

    # punctuation mode: ALL
    PUNCT_MODE=1

    label="word with same capitalization #1"
    text="CAPITAL LETTER"
    convertFile $((i++))

    label="word with same capitalization #2"
    text="capital Letter"
    convertFile $((i++))

    label="first word with capital letter and the remaining text as lower case #1"
    text="Capital letter"
    convertFile $((i++))

    label="first word with capital letter and the remaining text as lower case #2"
    text="CaPital letter"
    convertFile $((i++))

    label="first word all caps and the remaining text as lower case"
    text="CAPITAL letter"
    convertFile $((i++))

    label="text as lower case"
    text="capital letter"
    convertFile $((i++))

    # (2+3) + 250 + (1 + 1+1) = 256 + 2
    label="2 tlv, last capital tag splitted #1: abc + 125 é + A"
    text="abc${T125}A"
    convertFile $((i++))

    # (2+2) + 250 + (1+1 +1) = 256 + 1
    label="2 tlv + last capital tag splitted #2: ab + 125 é + A"
    text="ab${T125}A"
    convertFile $((i++))

    # ---> 
    echo "$i. BEGIN -CAPS +PUNCT V=104"
    # compatible with libinote 1.0.4
    VERSION_COMPAT=104
    CAPS_MODE=0
    PUNCT_MODE=1

    label="capital deactivated; first word with capital letter and the remaining text as lower case #2"
    text="CaPital letter"
    convertFile $((i++))

    # ---> 
    echo "$i. BEGIN +CAPS -BEEP -PUNCT"
    VERSION_COMPAT=-1
    CAPS_MODE=1
    unset CAPS_PREFIX
    # punctuation mode: NONE
    PUNCT_MODE=0

    label="capital activated; first word with capital letter and the remaining text as lower case #2"
    text="CaPital letter"
    convertFile $((i++))

    label="word with capital letter followed by a not-to-spell punctuation char"
    text="Voxin: "
    convertFile $((i++))

    label="lower case word followed by a not-to-spell punctuation char"
    text="voxin: "
    convertFile $((i++))

    # ---> 
    echo "$i. BEGIN -CAPS -PUNCT"
    VERSION_COMPAT=-1
    CAPS_MODE=0
    unset CAPS_PREFIX
    # punctuation mode: NONE
    PUNCT_MODE=0

    label="word with capital letter between not-to-spell punctuation char"
    text="(Voxin) "
    convertFile $((i++))

    label="lower case word between not-to-spell punctuation char"
    text="(voxin) "
    convertFile $((i++))
}

# <--- END CAPS ACTIVATED TESTS

leave() {
	echo "$1" && exit $2
}

testSSML() {
    local num=$1
    local text="$2"
    local punct_mode="$3"
    local tlv=$4
    local res=$(mktemp)

    echo "* SSML#$num. text='$text', punc:$punct_mode"

    if [ -n "$WITH_GDB" ]; then
	gdb -ex "b inote_get_type_length_value" -ex "b inote_push_text" -ex "b inote_convert_text_to_tlv" -ex "set args -s -p $punct_mode -t '$text' -o $res" ./text2tlv
	exit 0
    fi

    ./text2tlv -s -p $punct_mode -t "$text" -o "$res"
    diff -q "$tlv" "$res" || leave "SSML $num: KO" 1
    rm "$res"
}

convertText() {
	NUM=$1
	LABEL=$2
	TEXT="$3"
	PUNCT_MODE="$4"
	echo "* $NUM. $LABEL"
	echo -e "input:\n$TEXT"
	echo -e "tlv:"
	./text2tlv -p $PUNCT_MODE -t "$TEXT" | hexdump -Cv
}

convertFile() {
    NUM=$1

    local args
    local file=${TMPDIR}/test_$NUM

    echo -en "$text" > $file
    #		rm -f "$file.tlv" "$file.txt"
    
    if [ -z "$QUIET" ]; then	
	echo
	echo "* $NUM. $label"
	echo "input:"
	cat "$file"
	echo
    fi

    args="-p $PUNCT_MODE -i $file -o $file.tlv "
    [ -n "$VERSION_COMPAT" ] && [ "$VERSION_COMPAT" != -1 ] && args="$args -v $VERSION_COMPAT"
    [ -n "$CAPS_MODE" ] && [ "$CAPS_MODE" != 0 ] && args="$args -C"

    ./text2tlv $args
    
    if [ -z "$QUIET" ]; then	
	echo "tlv:"
	hexdump -Cv "$file.tlv"
    fi
    
    ./tlv2text -i "$file.tlv" -o "$file.txt" -c "$CAPS_PREFIX"
    if [ -z "$QUIET" ]; then	
	echo "output:"
	cat "$file.txt"
    fi
    
    diff -q "$file.tlv" res/$(basename "$file.tlv") || leave "text $NUM. $label (tlv): KO" 1
    diff -q "$file.txt" res/$(basename "$file.txt") || leave "text $NUM. $label (txt): KO" 1
    echo "text $NUM: OK"
    rm -f "$file.tlv" "$file.txt"
}


#PUNCT_MODE=0
PUNCT_MODE=1
# PUNCT_MODE=2

testCharset() {
	FILE=$1
	SEP=$2
	CHARSETS=$3
	FILE_EXPECTED=$4
	ERROR=$5
	# uncomment gdb if needed
	if [ -n "$WITH_GDB" ]; then	
		gdb -ex "b inote_push_text" -ex "b inote_convert_text_to_tlv" -ex "set args -c $CHARSETS -i $FILE -o $FILE.$SEP.tlv" ./text2tlv
		exit 0
	fi
	./text2tlv       -p 1 -c $CHARSETS -i "$FILE" -o "$FILE.$SEP.tlv"
	local err=$?
	if [ -n "$ERROR" ]; then
	    if [ "$err" = "$ERROR" ]; then
		echo "Expected error: OK"
		return
	    else
		leave "Expected error: KO" 1
	    fi
	fi
	./tlv2text -i "$FILE.$SEP.tlv" -o "$FILE.$SEP.txt"
	diff -q $FILE_EXPECTED $FILE.$SEP.txt
	if [ $? != 0 ]; then
		diff -u $FILE_EXPECTED $FILE.$SEP.txt		
		leave "$CHARSETS: KO" 1
	fi
	rm "$FILE.$SEP.tlv" "$FILE.$SEP.txt"
	echo "charset $CHARSETS: OK"
}

echo
echo "libinote: starting tests"
echo

# --> checking ssml mode

numSSML=1
TEXT="<speak>hello"
punctMode=0
testSSML "$numSSML" "$TEXT" "$punctMode" res/ssml.1.txt

numSSML=$((++numSSML))
TEXT="<"
punctMode=0
testSSML "$numSSML" "$TEXT" "$punctMode" res/ssml.2.txt

# --> checking erroneous entries
# UTF8
for i in a b c d e; do
    testCharset utf8_err6$i.txt 8-1 UTF-8:ISO-8859-1 res/utf8_err6$i.txt.8-1.txt
done

## Expected error: INOTE_INCOMPLETE_MULTIBYTE
testCharset utf8_err1.txt 8-1 UTF-8:ISO-8859-1 res/utf8_err1.txt $INOTE_INCOMPLETE_MULTIBYTE

# LATIN1
for i in a b c d e; do
    testCharset latin1_err6$i.txt 1-8 ISO-8859-1:ISO-8859-1 res/latin1_err6$i.txt.1-8.txt
done

testCharset latin1_err1.txt 1-8 ISO-8859-1:ISO-8859-1 res/latin1_err1.txt.1-8.txt
# <--

TMPDIR=$(mktemp -d)
filea8=${TMPDIR}/test_utf8_symbol
filea1=${TMPDIR}/test_latin1

# currently utf8-t character without implemented equivalent are filtered 
echo "cœur vaillant" > $filea8
echo "cur vaillant" > $filea1
testCharset $filea8 8-1 UTF-8:ISO-8859-1 $filea1

echo "Produits • La Boutique" > $filea8
echo "Produits  La Boutique" > $filea1
testCharset $filea8 8-1 UTF-8:ISO-8859-1 $filea1

# quote=0xe2 0x80 0x99
echo "l’ail" > $filea8
# quote=0x27
echo "l'ail" > $filea1
testCharset $filea8 8-1 UTF-8:ISO-8859-1 $filea1

cat <<EOF>$filea8
«
»
‘
’
‚
“
”
„
‹
›
Ꮙ
‛
‟
⍘
⍞
❛
❜
❝
❞
❮
❯
〝
〞
〟
ꐄ
ꐅ
ꐆ
ꐇ
＂
󠀢
EOF

cat <<EOF>$filea1
'
'
'
'
'
'
'
'
'
'
'
'
'
'
'
'
'
'
'
'
'
'
'
'
'
'
'
'
'
'
EOF
testCharset $filea8 8-1 UTF-8:ISO-8859-1 $filea1

testCharset $file1 1-1 ISO-8859-1:ISO-8859-1 $file1
testCharset $file8 8-8 UTF-8:UTF-8 $file8
testCharset $file1 1-8 ISO-8859-1:UTF-8 $file8
testCharset $file8 8-1 UTF-8:ISO-8859-1 $file1

testSentence

# testCharset $file1_orig 1-1 ISO-8859-1:ISO-8859-1 $file1_orig
# testCharset $file8_orig 8-8 UTF-8:UTF-8 $file8_orig
# testCharset $file1_orig 1-8 ISO-8859-1:UTF-8 $file8_orig
# testCharset $file8_orig 8-1 UTF-8:ISO-8859-1 $file1_orig

# if [ -n "$WITH_GDB" ]; then
#     TMPFILE=${TMPDIR}/test_last
#     echo -en "${testArray[-1]}" > $TMPFILE
#     gdb -ex "b inote_push_text" -ex "b inote_convert_text_to_tlv" -ex "set args -p $PUNCT_MODE -i '$TMPFILE' -o '$TMPFILE.tlv'" -x gdb_commands ./text2tlv
#else
# #	gdb -ex "b inote_push_text" -ex "b inote_convert_text_to_tlv" -ex "set args -p $PUNCT_MODE -i '$TMPFILE' -o '$TMPFILE.tlv'" ./text2tlv
# else
# 	j=0
# 	for i in "${testArray[@]}"; do
# 		TMPFILE=${TMPDIR}/test_$j
# 		echo -en "$i" > $TMPFILE
# 		#		rm -f "$FILE.tlv" "$FILE.txt"
# 		CAPS_MODE=0
# 		VERSION_COMPAT=-1
# 		[ "$j" -ge "$CAPITAL_BEGIN" ] && [ "$j" -lt "$CAPITAL_END" ] && CAPS_MODE=1

# 		# version 1.0.4 is not compatible with the TLV for capital letters (from version 1.1.0)
# 		# Corresponds to sequence WITHOUT_CAPITAL_FEATURE_BEGIN/END and VERSION_COMPAT=104
# 		[ "$j" -ge "$WITHOUT_CAPITAL_FEATURE_BEGIN" ] && [ "$j" -lt "$WITHOUT_CAPITAL_FEATURE_END" ] && VERSION_COMPAT=104		

# 		# version 1.1.0 is compatible with TLV for capital letters
# 		# Corresponds to sequence CAPITAL_ACTIVATED_BEGIN/END and VERSION_COMPAT=110
# 		#
# 		# we add PUNCT_MODE=0 to specifically check a capitalized text followed by a not-to-spell
# 		# punctuation char; e.g. "Capital: " 
# 		[ "$j" -ge "$CAPITAL_ACTIVATED_BEGIN" ] && [ "$j" -lt "$CAPITAL_ACTIVATED_END" ] && VERSION_COMPAT=110 && PUNCT_MODE=0
# 		convertFile $j "${testLabel[$j]}" "$TMPFILE" $PUNCT_MODE $CAPS_MODE $VERSION_COMPAT
# 		j=$((j+1))
# 	done
#fi

# cat /tmp/libinote.log.*

echo
echo "libinote: PASS"
echo

