#!/bin/bash

touch /tmp/libinote.ok
rm -f /tmp/libinote.log.*

testFileUrl="http://abu.cnam.fr/cgi-bin/donner_unformated?nddp1"
testFileDest=nddp1.txt.iso-8859-1

file_utf_8=${testFileDest%.iso-8859-1}.utf-8
if [ ! -e "$file_utf_8" ]; then
	wget -O - "$testFileUrl" | sed -e 's/[\r]//g' -e "/^$/d" > "$testFileDest"
	iconv -f iso-8859-1 -t utf-8 -o "$file_utf_8" "$testFileDest"  
fi

T125="ééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééé"
T126="éééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééé"
T127="ééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééé"
T256=${T127}${T127}éé

unset testArray
i=0

# testLabel[$i]="utf-8 text"
# testArray[$((i++))]="Un éléphant"

# testLabel[$i]="utf-8 text + filtered annotation + tag + punctuation"
# testArray[$((i++))]="\`gfa1 \`gfa2 \`Pf2()? <speak>Un &lt;éléphant&gt; (1)</speak>"

# testLabel[$i]="utf-8 text + annotation"
# testArray[$((i++))]="\`v1 Un \`v2 éléphant"

# testLabel[$i]="1 tlv for 127 é (header=2 bytes + value=254 bytes)"
# testArray[$((i++))]=${T127}

# testLabel[$i]="2 tlv: 127 é + a"
# testArray[$((i++))]=${T127}a

# testLabel[$i]="1 tlv: a + 125 é"
# testArray[$((i++))]=a${T125}

# testLabel[$i]="2 tlv, last utf-8 splitted: a + 125 é + 4 bytes utf8 char 𪚥"
# testArray[$((i++))]=a${T125}𪚥

testLabel[$i]="1025 bytes: a + 512 é"
testArray[$((i++))]=$(echo -n "a$T256$T256")

# testLabel[$i]="6 bytes: é + 2 erroneous utf-8 bytes + é"
# testArray[$((i++))]=$(echo -en "é\xca\xfeé")

# testLabel[$i]="256 bytes: 127 é + 2 erroneous bytes"
# testArray[$((i++))]=$(echo -en "${T127}\xca\xfe")

# testLabel[$i]="257 bytes: a + 127 é + 2 erroneous bytes"
# testArray[$((i++))]=$(echo -en "a${T127}\xca\xfe")

convertText() {
	NUM=$1
	LABEL=$2
	TEXT="$3"
	PUNCT_MODE="$4"
	echo "* $NUM. $LABEL"
	echo -e "text:\n$TEXT"
	echo -e "tlv:"
	./text2tlv -p $PUNCT_MODE -t "$TEXT" | hexdump -Cv
}

convertFile() {
	NUM=$1
	LABEL=$2
	FILE=$3
	PUNCT_MODE=$4
	echo
	echo "* $NUM. $LABEL"
	echo "text:"
	cat "$FILE"
	echo
	./text2tlv -p $PUNCT_MODE -i "$FILE" -o "$FILE.tlv"
	echo "tlv:"
	hexdump -Cv "$FILE.tlv"
	./tlv2text -i "$FILE.tlv" -o "$FILE.txt"
	echo "text:"
	hexdump -Cv "$FILE.txt"
}

#PUNCT_MODE=0
PUNCT_MODE=1
# PUNCT_MODE=2

# if [ "$1" = "-g" ]; then
# 	text="${testArray[0]}"
# 	gdb -ex "set args -p $PUNCT_MODE -t '${testArray[-1]}'" -x gdb_commands ./inote
# else
#	j=0
# 	for i in "${testArray[@]}"; do
#		j=$((j+1))
#
# 		convertText $j "${testLabel[$j]}" "$i" $PUNCT_MODE
# 	done
# fi

TMPFILE=$(mktemp)

if [ "$1" = "-g" ]; then
	echo -en "${testArray[-1]}" > $TMPFILE
	gdb -ex "set args -p $PUNCT_MODE -i '$TMPFILE' -o '$TMPFILE.tlv'" -x gdb_commands ./text2tlv
else
	j=0
	for i in "${testArray[@]}"; do
		echo -en "$i" > $TMPFILE
#		rm -f "$FILE.tlv" "$FILE.txt" 
		convertFile $j "${testLabel[$j]}" "$TMPFILE" $PUNCT_MODE
		j=$((j+1))
	done
fi

# cat /tmp/libinote.log.*

