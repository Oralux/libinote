#!/bin/bash

touch /tmp/libinote.ok

doTest() {
	TEXT="$1"
	rm /tmp/libinote.log.*
	./inote "$TEXT"
	echo "----------------------------------"
	echo -e "text:\n$TEXT"
	cat /tmp/libinote.log.*
}

doTest "Un éléphant"
doTest "<speak>Un éléphant ! təmei̥ɾou̥</speak>"
