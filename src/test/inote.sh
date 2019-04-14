#!/bin/bash

touch /tmp/libinote.ok

unset testArray
i=0
testArray[$((i++))]="Un éléphant"
testArray[$((i++))]="<speak>Un &quot;éléphant&quot; ! təmei̥ɾou̥</speak>"

doTest() {
	TEXT="$1"
	rm /tmp/libinote.log.*
	./inote "$TEXT"
	echo "----------------------------------"
	echo -e "text:\n$TEXT"
	cat /tmp/libinote.log.*
}

if [ "$1" = "-g" ]; then
	text="${testArray[0]}"
	gdb -ex "set args \"${testArray[-1]}\"" -x gdb_commands ./inote
else
	for i in "${testArray[@]}"; do
		doTest "$i"
	done
fi

