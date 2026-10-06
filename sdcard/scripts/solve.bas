10 print "== 解方程 v1.0 =="
20 print "1 一元一次  2 一元二次"
30 print "3 二元一次组  0 退出"
40 input m
50 if m=0 then end
60 if m=1 then gosub 200
70 if m=2 then gosub 300
80 if m=3 then gosub 400
90 goto 10
100 end
200 print "ax+b=0  输入 a"
210 input a
220 print "输入 b"
230 input b
240 if a=0 then print "无解" : return
250 x=(-b)/a
260 print "x="; x
270 return
300 print "ax2+bx+c=0 输入 a"
310 input a
320 print "输入 b"
330 input b
340 print "输入 c"
350 input c
360 d=b*b-4*a*c
370 if d<0 then print "无实根" : return
380 if d=0 then print "x="; (-b)/(2*a) : return
390 print "x1="; (-b+sqr(d))/(2*a)
400 print "x2="; (-b-sqr(d))/(2*a)
410 return
420 print "a1x+b1y=c1 输入 a1"
430 input a1
440 print "输入 b1"
450 input b1
460 print "输入 c1"
470 input c1
480 print "a2x+b2y=c2 输入 a2"
490 input a2
500 print "输入 b2"
510 input b2
520 print "输入 c2"
530 input c2
540 dd=a1*b2-a2*b1
550 if dd=0 then print "无唯一解" : return
560 print "x="; (c1*b2-c2*b1)/dd
570 print "y="; (a1*c2-a2*c1)/dd
580 return
