.text


add X2, X4, 7
cbnz X2, bar
add X2, X0, 10

foo:
add X8, X9, 11
b bar

bar:
add X10, X2, X8

add X2, X4, 7
cbnz X3, bar2
add X8, X9, 9
cbz X3, bar2
add X2, X0, 10

foo2:
add X8, X9, 11
b bar2

bar2:
add X10, X2, X8

mov X9, #3
mov X10, #8
sub X11, X0, #2
sdiv X13, X10, X11
udiv X14, X10, X11
sdiv X15, X10, X9
udiv X16, X10, X9

add X9, X8, 10
add X10, X8, 7
add X11, X8, 23
eor X12, X10, X11
eor X13, X10, X9

mov X1, 0x1000
lsl X1, X1, 16
mov X10, 10
stur X10, [X1, 0x0]
mov X12, 2
stur X12, [X1, 0x10]
ldur X13, [X1, 0x0]
ldur X14, [X1, 0x10]

mov X1, 0x1000
lsl X1, X1, 16
mov X10, 0x1234
stur X10, [X1, 0x0]
sturb W10, [X1, 0x6]
ldur X13, [X1, 0x0]
ldurb W14, [X1, 0x6]


mov X1, 0x1000
lsl X1, X1, 16
mov X10, 0x1234
stur X10, [X1, 0x0]
sturh W10, [X1, 0x6]
ldur X13, [X1, 0x0]
ldurh W14, [X1, 0x6]

add X9, X8, 10
add X10, X8, 7
lsl X12, X10, 1
lsl X13, X9, 2
lsl X14, X9, 0

add X9, X8, 0x100
lsr X13, X9, 2
lsr X14, X9, 0
lsr X15, X9, 1
lsr X16, X9, 2
lsr X17, X9, 33
lsr X18, X9, 63

mov X10, #8
mov X11, #9
mul X12, X10, X11
mul X13, X10, X10

add X9, X8, 10
add X10, X8, 7
add X11, X8, 23
orr X12, X10, X11
orr X13, X10, X9

mov X1, 0x1000
lsl X1, X1, 16
mov x28, 0x1000
lsl x28, x28, 16
mov x0, #0x2174
mov X10, 10
stur X10, [X1, 0x0]
stur W0, [x28, 0xc]
stur X12, [X1, 0x10]
ldur X13, [X1, 0x0]
ldur X14, [X1, 0x10]

mov X1, 0x1000
lsl X1, X1, 16
mov X10, 0x1234
stur X10, [X1, 0x0]
sturb W10, [X1, 0x6]
ldur X13, [X1, 0x0]
ldur W14, [X1, 0x4]
ldurb W15, [X1, 0x6]

mov X1, 0x1000
lsl X1, X1, 16
mov X10, 0x1234
stur X10, [X1, 0x0]
sturh W10, [X1, 0x6]
ldur X13, [X1, 0x0]
ldurh W14, [X1, 0x6]

mov X1, 0x1000
lsl X1, X1, 16
mov x0, 0x2174
lsl x0, x0, 32
add x0, x0, 0x126
stur w0, [x1, 0x0]
stur x0, [X1, 0x10]
ldur X13, [X1, 0x0]
ldur X14, [X1, 0x10]
HLT 0

