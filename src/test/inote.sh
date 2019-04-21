#!/bin/bash

#touch /tmp/libinote.ok

testFileUrl="http://abu.cnam.fr/cgi-bin/donner_unformated?nddp1"
testFileDest=nddp1.txt.iso-8859-1

file_utf_8=${testFileDest%.iso-8859-1}.utf-8
if [ ! -e "$file_utf_8" ]; then
	wget -O - "$testFileUrl" | sed -e 's/[\r]//g' -e "/^$/d" > "$testFileDest"
	iconv -f iso-8859-1 -t utf-8 -o "$file_utf_8" "$testFileDest"  
fi

unset testArray
i=0
testArray[$((i++))]="Un éléphant"
testArray[$((i++))]="\`gfa1 \`gfa2 \`Pf2()? <speak>Un &lt;éléphant&gt; (1)</speak>"
testArray[$((i++))]="\`v1 Un \`v2 éléphant"

# 127 é (2 bytes (header) + 254 bytes = tlv element)
testArray[$((i++))]="ééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééé"

# 2 tlv: 127 é + a
# tlv#1: 127 é
# tlv#2: a 
testArray[$((i++))]="éééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééa"

# 2 tlv, last utf-8 potentially splitted: a + 127 é
# tlv#1: a + 126 é
# tlv#2: é 
testArray[$((i++))]="aééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééé"

# 254 é
testArray[$((i++))]="éééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééé"

# 256 é
testArray[$((i++))]="éééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééé"


convertText() {
	TEXT="$1"
	PUNCT_MODE="$2"
#	rm -f /tmp/libinote.log.*
	echo "----------------------------------"
	echo -e "text:\n$TEXT"
	echo -e "tlv:"
	./inote -p $PUNCT_MODE -t "$TEXT" | hexdump -Cv
}

convertFile() {
	FILE="$1"
	PUNCT_MODE="$2"
#	rm -f /tmp/libinote.log.*
	echo "----------------------------------"
	echo -e "file:\n$FILE"
	echo -e "tlv:"
	./inote -p $PUNCT_MODE -i "$FILE" -o "$FILE.tlv"
	ls -l "$FILE" "$FILE.tlv"
}

#PUNCT_MODE=0
PUNCT_MODE=1
# PUNCT_MODE=2

# if [ "$1" = "-g" ]; then
# 	text="${testArray[0]}"
# 	gdb -ex "set args -p $PUNCT_MODE -t '${testArray[-1]}'" -x gdb_commands ./inote
# else
# 	for i in "${testArray[@]}"; do
# 		convertText "$i" $PUNCT_MODE
# 	done
# fi

if [ "$1" = "-g" ]; then
	gdb -ex "set args -p $PUNCT_MODE -i '$file_utf_8' -o '$file_utf_8.tlv'" -x gdb_commands ./inote
else
	convertFile "$file_utf_8" $PUNCT_MODE
fi

