#!/bin/bash

touch $HOME/libinote.ok
rm -f /tmp/libinote.log.*

#unset QUIET
QUIET=1

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




T125="ééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééé"
T126="éééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééé"
T127="ééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééé"
T256=${T127}${T127}éé

unset testArray testRes
i=0

testLabel[$i]="utf-8 text"
testArray[$((i++))]="   Un éléphant"
testRes[$((i++))]="Un éléphant"

testLabel[$i]="utf-8 text"
testArray[$((i++))]=",   Un éléphant"
testRes[$((i++))]=",   Un éléphant"

testLabel[$i]="utf-8 text + filtered annotation + tag + punctuation"
testArray[$((i++))]="  \`gfa1 \`gfa2 \`Pf2()? <speak>Un &lt;éléphant&gt; (1)</speak>"
testRes[$((i++))]="Un <éléphant> (1)"

testLabel[$i]="utf-8 text + annotation"
testArray[$((i++))]=" \`v1 Un \`v2 éléphant"
testRes[$((i++))]=""

testLabel[$i]="1 tlv for 127 é (header=2 bytes + value=254 bytes)"
testArray[$((i++))]=${T127}
testRes[$((i++))]=""

testLabel[$i]="2 tlv: 127 é + a"
testArray[$((i++))]=${T127}a
testRes[$((i++))]=""

testLabel[$i]="1 tlv: a + 125 é"
testArray[$((i++))]=a${T125}
testRes[$((i++))]=""

testLabel[$i]="2 tlv, last utf-8 splitted: a + 125 é + 4 bytes utf8 char 𪚥"
testArray[$((i++))]=a${T125}𪚥
testRes[$((i++))]=""

testLabel[$i]="1025 bytes: a + 512 é"
testArray[$((i++))]=$(echo -n "a$T256$T256")
testRes[$((i++))]=""

testLabel[$i]="6 bytes: é + 2 erroneous utf-8 bytes + é"
testArray[$((i++))]=$(echo -en "é\xca\xfeé")
testRes[$((i++))]=""

testLabel[$i]="256 bytes: 127 é + 2 erroneous bytes"
testArray[$((i++))]=$(echo -en "${T127}\xca\xfe")
testRes[$((i++))]=""

testLabel[$i]="257 bytes: a + 127 é + 2 erroneous bytes"
testArray[$((i++))]=$(echo -en "a${T127}\xca\xfe")
testRes[$((i++))]=""

testLabel[$i]="utf-8 text + language switching annotation"
testArray[$((i++))]="Un éléphant \`l0x12345678 One elephant"
testRes[$((i++))]="Un éléphant"


leave() {
	echo "$1" && exit $2
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
	LABEL=$2
	FILE=$3
	PUNCT_MODE=$4
	if [ -z "$QUIET" ]; then	
		echo
		echo "* $NUM. $LABEL"
		echo "input:"
		cat "$FILE"
		echo
	fi
	
	./text2tlv -p $PUNCT_MODE -i "$FILE" -o "$FILE.tlv"

	if [ -z "$QUIET" ]; then	
		echo "tlv:"
		hexdump -Cv "$FILE.tlv"
	fi

	./tlv2text -i "$FILE.tlv" -o "$FILE.txt"
	if [ -z "$QUIET" ]; then	
		echo "output:"
		cat "$FILE.txt"
	fi

	diff -q "$FILE.tlv" res/$(basename "$FILE.tlv") || leave "text $NUM. $LABEL (tlv): KO" 1
	diff -q "$FILE.txt" res/$(basename "$FILE.txt") || leave "text $NUM. $LABEL (txt): KO" 1
	echo "text $NUM: OK"
}


#PUNCT_MODE=0
PUNCT_MODE=1
# PUNCT_MODE=2

testCharset() {
	FILE=$1
	SEP=$2
	CHARSETS=$3
	FILE_EXPECTED=$4
	# uncomment gdb if needed
	# gdb -ex "b inote_push_text" -ex "b inote_convert_text_to_tlv" -ex "set args -c $CHARSETS -i $FILE -o $FILE.$SEP.tlv" ./text2tlv
	./text2tlv       -p 1 -c $CHARSETS -i "$FILE" -o "$FILE.$SEP.tlv"
	./tlv2text -i "$FILE.$SEP.tlv" -o "$FILE.$SEP.txt"
	diff -q $FILE_EXPECTED $FILE.$SEP.txt || leave "$CHARSETS: KO" 1
	rm "$FILE.$SEP.tlv" "$FILE.$SEP.txt"
	echo "charset $CHARSETS: OK"
}

echo
echo "libinote: starting tests"
echo

TMPDIR=$(mktemp -d)
filea8=${TMPDIR}/test_utf8_symbol
filea1=${TMPDIR}/test_latin1

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

# testCharset $file1_orig 1-1 ISO-8859-1:ISO-8859-1 $file1_orig
# testCharset $file8_orig 8-8 UTF-8:UTF-8 $file8_orig
# testCharset $file1_orig 1-8 ISO-8859-1:UTF-8 $file8_orig
# testCharset $file8_orig 8-1 UTF-8:ISO-8859-1 $file1_orig

if [ "$1" = "-g" ]; then
	TMPFILE=${TMPDIR}/test_last
	echo -en "${testArray[-1]}" > $TMPFILE
	gdb -ex "b inote_push_text" -ex "b inote_convert_text_to_tlv" -ex "set args -p $PUNCT_MODE -i '$TMPFILE' -o '$TMPFILE.tlv'" -x gdb_commands ./text2tlv
#	gdb -ex "b inote_push_text" -ex "b inote_convert_text_to_tlv" -ex "set args -p $PUNCT_MODE -i '$TMPFILE' -o '$TMPFILE.tlv'" ./text2tlv
else
	j=0
	for i in "${testArray[@]}"; do
		TMPFILE=${TMPDIR}/test_$j
		echo -en "$i" > $TMPFILE
#		rm -f "$FILE.tlv" "$FILE.txt" 
		convertFile $j "${testLabel[$j]}" "$TMPFILE" $PUNCT_MODE
		j=$((j+1))
	done
fi

# cat /tmp/libinote.log.*

echo
echo "libinote: PASS"
echo

