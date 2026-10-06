10 sv$ = "http://ser270472228666.ahostxg.idc001.site/chat/api.php"
100 color 100,150,255
110 clear
120 text 8,8,"CHAT for Memoria OS"
130 color 255,255,255
140 print "chat v2 - 掌机端(文字+语音)"
150 print "WiFi: "; wifi_ssid$
200 if wifistat() = 0 then print "WiFi未连接" : end
210 print "验服务器..."
240 httpget sv$ + "?a=ping", r$
250 if r$ <> "pong chat v1" then print "服务器不通" : end
280 lg = 0
290 vmid$ = ""
300 while lg = 0
310   print "1=登录 2=注册 0=退出"
320   input m
330   if m = 2 then gosub 500
340   if m = 1 then gosub 600
350   if m = 0 then end
360 wend
400 print "--- 进入 #"; rm$; " ---"
410 while 1
420   gosub 800
430   print "[1]发言 [2]刷新 [3]听语音"
435   print "[4]发语音 [0]退出"
440   input m
450   if m = 1 then gosub 700
455   if m = 3 then gosub 900
457   if m = 4 then gosub 1000
460   if m = 0 then end
470 wend
500 print "用户名:"
510 input nu$
520 print "密码(4位以上):"
530 input np$
540 if len(np$) < 4 then print "密码太短" : return
550 url$ = sv$ + "?a=reg&u=" + nu$
560 url$ = url$ + "&p=" + np$
570 httpget url$, r$
580 if left$(r$,2) = "ok" then print "注册成功,请登录" else print r$
595 return
600 print "用户名:"
610 input u$
620 print "密码:"
630 input p$
640 print "房间(回车=lobby):"
650 input rm$
660 if rm$ = "" then rm$ = "lobby"
670 url$ = sv$ + "?a=login&u=" + u$
680 url$ = url$ + "&p=" + p$
690 httpget url$, r$
695 if left$(r$,2) = "ok" then lg = 1 : print "登录成功" else print r$
698 return
700 print "内容:"
710 input b2$
720 if b2$ = "" then return
730 gosub 1700
740 url$ = sv$ + "?a=send&u=" + u$
750 url$ = url$ + "&p=" + p$ + "&room=" + rm$
760 url$ = url$ + "&body=" + b3$
770 httpget url$, r$
780 if left$(r$,2) = "ok" then lm = val(mid$(r$,4)) else print r$
795 return
800 url$ = sv$ + "?a=get&u=" + u$
810 url$ = url$ + "&p=" + p$ + "&room=" + rm$
820 url$ = url$ + "&after=" + str$(lm)
830 httpget url$, r$
840 if httpstat() <> 200 then print "网络错误 "; httpstat() : return
850 if left$(r$,2) <> "ok" then print r$ : return
860 bd2$ = mid$(r$, 4)
870 while len(bd2$) > 0
880   gosub 1400
885   gosub 1500
890 wend
895 return
900 if vmid$ = "" then print "还没有语音" : return
910 print "下载语音#"; vmid$; "..."
920 url$ = sv$ + "/uploads/voice/v" + vmid$ + ".wav"
930 httpdl url$, "/mem_fat/vr.wav"
940 if httpstat() <> 200 then print "下载失败 "; httpstat() : return
950 print "播放中..."
960 play "/mem_fat/vr.wav"
995 return
1000 print "录音5秒,请说话..."
1010 record "/mem_fat/vr.wav", 5
1020 url$ = sv$ + "?a=voice&u=" + u$
1030 url$ = url$ + "&p=" + p$ + "&room=" + rm$
1040 url$ = url$ + "&sec=5"
1050 httpup url$, "/mem_fat/vr.wav", r$
1060 if left$(r$,2) = "ok" then print "已发送" else print "失败 "; r$
1095 return
1400 nl$ = chr$(10) : ln2 = len(bd2$) : i4 = 1 : fnd = 0
1410 while (i4 <= ln2) * (fnd = 0)
1420   if mid$(bd2$, i4, 1) = nl$ then fnd = 1 else i4 = i4 + 1
1430 wend
1440 if fnd = 1 then line$ = left$(bd2$, i4 - 1) : bd2$ = mid$(bd2$, i4 + 1)
1450 if fnd = 0 then line$ = bd2$ : bd2$ = ""
1460 return
1500 pp = 1
1510 gosub 1600 : lm = val(f$) : cm$ = f$
1520 gosub 1600 : sn$ = f$
1530 gosub 1600 : ty$ = f$
1540 if len(line$) >= pp then bd$ = right$(line$, len(line$) - pp + 1) else bd$ = ""
1550 if ty$ = "v" then gosub 1570 else print "["; sn$; "] "; bd$
1560 return
1570 vmid$ = cm$
1580 print "["; sn$; "] 语音#"; cm$; " 按3听"
1590 return
1600 f$ = "" : ln3 = len(line$)
1610 while (pp <= ln3) * (mid$(line$, pp, 1) <> "|")
1620   f$ = f$ + mid$(line$, pp, 1)
1630   pp = pp + 1
1640 wend
1650 pp = pp + 1
1660 return
1700 b3$ = "" : i5 = 1
1710 while i5 <= len(b2$)
1720   if mid$(b2$, i5, 1) = " " then b3$ = b3$ + "+" else b3$ = b3$ + mid$(b2$, i5, 1)
1730   i5 = i5 + 1
1740 wend
1750 return
