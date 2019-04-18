#!/bin/bash

#touch /tmp/libinote.ok

unset testArray
i=0
testArray[$((i++))]="Un éléphant"
testArray[$((i++))]="\`gfa1 \`gfa2 \`Pf2()? <speak>Un &lt;éléphant&gt; (1)</speak>"
testArray[$((i++))]="\`v1 Un \`v2 éléphant"

doTest() {
	TEXT="$1"
	PUNCT_MODE="$2"
#	rm -f /tmp/libinote.log.*
	echo "----------------------------------"
	echo -e "text:\n$TEXT"
	echo -e "tlv:"
	./inote -p $PUNCT_MODE -t "$TEXT" | hexdump -Cv
}

#PUNCT_MODE=0
PUNCT_MODE=1
# PUNCT_MODE=2

if [ "$1" = "-g" ]; then
	text="${testArray[0]}"
	gdb -ex "set args -p $PUNCT_MODE -t '${testArray[-1]}'" -x gdb_commands ./inote
else
	for i in "${testArray[@]}"; do
		doTest "$i" $PUNCT_MODE
	done
fi

