#!/bin/bash

exeFile=./cmake-build-debug/OS_HW2

$exeFile vault.vlt init 40000B
$exeFile vault.vlt add "files/input.txt"
$exeFile vault.vlt add "files/input1.txt"
$exeFile vault.vlt add "files/input2.txt"
$exeFile vault.vlt add "files/input3.txt"
$exeFile vault.vlt add "files/input4.txt"
$exeFile vault.vlt add "files/input5.txt"
$exeFile vault.vlt add "files/6.txt"
$exeFile vault.vlt add "files/7.txt"
$exeFile vault.vlt add "files/8.txt"
$exeFile vault.vlt add "files/9.txt"
$exeFile vault.vlt list
$exeFile vault.vlt rm "input1.txt"
$exeFile vault.vlt rm "input3.txt" 
$exeFile vault.vlt rm "input5.txt"
$exeFile vault.vlt rm "6.txt"
$exeFile vault.vlt rm "8.txt"
$exeFile vault.vlt add "files/9.txt"
$exeFile vault.vlt fetch "9.txt"
assertNull $(diff "9.txt" "files/9.txt")
echo $(diff "9.txt" "files/9.txt")
assertNotSame "0" $?
$exeFile vault.vlt list
$exeFile vault.vlt defrag