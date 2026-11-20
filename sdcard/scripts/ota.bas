10 rem OTA 发布台 v1.0 掌机端固件下载与发布
20 rem @file ota.bas  依赖云端 ota/api.php
30 sv$ = "http://ser270472228666.ahostxg.idc001.site/chat/ota/api.php"
40 ky$ = "cfy20120331"
50 dim nm$(12),vr$(12),sz$(12),kd$(12)
60 nc = 0 : ci = 0 : bf = 1 : so = 0
70 msg$ = "准备就绪"
80 if wifistat() = 0 then msg$ = "WiFi 未连接,去系统设置"
90 gosub 2000 : gosub 1000
100 while 1
110   gosub 2000
120   k = key()
125   while k = 0
127     delay 50
129     k = key()
130   wend
140   if k = 27 then end
150   if k = 1 then gosub 400
160   if k = 2 then gosub 470
170   if k = 3 then gosub 540
180   if k = 4 then gosub 580
190   if k = 13 then gosub 500
200 wend
400 rem 上移
410 ci = ci - 1
420 if ci < 0 then ci = nc - 1
430 if nc = 0 then ci = 0
440 if ci < so then so = ci
450 if ci > so + 2 then so = ci - 2
460 return
470 rem 下移
480 ci = ci + 1
490 if ci > nc - 1 then ci = 0
495 if nc = 0 then ci = 0
497 if ci > so + 2 then so = ci - 2
498 if ci < so then so = ci
499 return
500 if bf = 0 then gosub 1000 : return
510 if bf = 1 then gosub 4000 : return
520 if bf = 2 then gosub 5000 : return
530 return
540 rem 按钮焦点左移
550 bf = bf - 1
560 if bf < 0 then bf = 2
570 return
580 rem 按钮焦点右移
590 bf = bf + 1
595 if bf > 2 then bf = 0
597 return
1000 rem 拉云端清单
1010 if wifistat() = 0 then msg$ = "WiFi 未连接" : return
1020 msg$ = "正在连接云端..."
1030 gosub 2000
1040 httpget sv$ + "?a=ping", r$
1050 if left$(r$,4) <> "pong" then msg$ = "!服务器不通" : return
1060 httpget sv$ + "?a=list&k=" + ky$, r$
1070 if left$(r$,2) <> "ok" then msg$ = "!" + left$(r$,24) : return
1080 gosub 3000
1090 if nc = 0 then msg$ = "云端暂无待发布包"
1100 if nc > 0 then msg$ = "已同步 " + str$(nc) + " 个包"
1110 return
2000 rem 整屏重绘
2005 clear
2006 color 244,246,251 : fill 0,0,480,304
2010 for g = 0 to 9
2015   color 91 + g * 5, 108 - g * 2, 247 - g
2020   fill g * 48, 0, 48, 60
2025 next g
2030 color 255,255,255 : fill 20,14,30,30
2035 color 91,108,247 : img 3,27,21
2040 color 255,255,255 : lbl 1,60,10,"OTA 发布台",0,2
2045 color 208,216,255 : text 60,44,"固件发布 · 云端同步"
2050 st$ = "离线" : color 239,68,68
2055 if wifistat() = 1 then st$ = "在线" : color 60,220,130
2060 img 1,394,20
2065 color 255,255,255 : text 416,22,st$
2070 color 255,255,255 : fill 12,68,456,146
2072 color 226,230,240 : rect 12,68,456,146
2074 color 22,24,44 : text 26,76,"云端待发布"
2076 color 138,144,168 : text 388,76,"共 " + str$(nc) + " 项"
2078 color 226,230,240 : line 26,96,458,96
2080 if nc = 0 then gosub 2700
2082 i = so
2084 while i < nc
2086   if i < so + 3 then gosub 2500
2088   i = i + 1
2090 wend
2100 color 91,108,247 : text 14,220,msg$
2130 bt = 0 : bx = 12 : bl$ = "刷新" : gosub 2800
2140 bt = 1 : bx = 164 : bl$ = "下载" : gosub 2800
2150 bt = 2 : bx = 316 : bl$ = "发布" : gosub 2800
2160 color 138,144,168
2170 text 12,282,"上下选择 左右切按钮 回车执行 Esc 退出"
2190 return
2500 rem 画一行条目
2510 y = 104 + (i - so) * 34
2520 if i = ci then color 238,240,255 : fill 22,y,436,32
2530 if i = ci then color 91,108,247 : fill 22,y,4,32
2540 n$ = nm$(i)
2550 if len(n$) > 20 then n$ = left$(n$,20)
2560 color 22,24,44 : text 36,y + 1,n$
2570 kb = int(val(sz$(i)) / 1024)
2580 d$ = vr$(i) + " · " + str$(kb) + "K · " + kd$(i)
2590 color 138,144,168 : text 36,y + 17,d$
2595 color 91,108,247 : text 424,y + 8,">"
2600 return
2700 rem 空态
2710 color 138,144,168 : text 158,150,"暂无待发布包,点刷新"
2720 return
2800 rem 画按钮 bt
2810 if bt = bf then color 91,108,247 : fill bx,238,152,40
2820 if bt = bf then color 255,255,255 : rect bx,238,152,40
2830 if bt <> bf then color 255,255,255 : fill bx,238,152,40
2840 if bt <> bf then color 216,221,236 : rect bx,238,152,40
2850 if bt = bf then color 255,255,255 else color 22,24,44
2860 text bx + 60,250,bl$
2870 return
3000 rem 解析行协议
3010 nc = 0 : ci = 0 : so = 0 : bd$ = ""
3020 if left$(r$,2) = "ok" then bd$ = mid$(r$,3)
3030 while len(bd$) > 0
3040   gosub 3100
3050   if ln$ <> "" then gosub 3200
3060 wend
3070 return
3100 rem 取一行到 ln$
3110 ln$ = "" : i2 = 1 : fnd = 0
3120 while (i2 <= len(bd$)) * (fnd = 0)
3130   c$ = mid$(bd$,i2,1)
3140   if c$ = chr$(10) then fnd = 1 else ln$ = ln$ + c$
3150   i2 = i2 + 1
3160 wend
3170 if fnd = 1 then bd$ = mid$(bd$,i2) else bd$ = ""
3180 if left$(ln$,1) = chr$(13) then ln$ = mid$(ln$,2)
3190 return
3200 rem 拆字段到数组
3210 if nc > 11 then return
3220 p = 1 : gosub 3300 : nm$(nc) = f$
3230 gosub 3300 : vr$(nc) = f$
3240 gosub 3300 : sz$(nc) = f$
3250 gosub 3300 : kd$(nc) = f$
3260 nc = nc + 1
3270 return
3300 rem 取下一个 | 字段
3310 f$ = "" : n3 = len(ln$)
3320 while (p <= n3) * (mid$(ln$,p,1) <> "|")
3330   f$ = f$ + mid$(ln$,p,1)
3340   p = p + 1
3350 wend
3360 p = p + 1
3370 return
4000 rem 下载选中包到 TF 卡
4010 if nc = 0 then msg$ = "列表为空" : return
4020 n$ = nm$(ci)
4030 msg$ = "下载中: " + n$ : gosub 2000
4040 u$ = sv$ + "?a=dl&k=" + ky$ + "&f=" + n$
4050 httpdl u$, "/mem_fat/" + n$
4060 if httpstat() <> 200 then msg$ = "!下载失败 " + str$(httpstat()) : return
4070 msg$ = "已存 TF 卡 /mem_fat/" + n$
4080 return
5000 rem 发布会选中包到 OTA 仓
5010 if nc = 0 then msg$ = "列表为空" : return
5020 n$ = nm$(ci)
5030 msg$ = "发布中: " + n$ : gosub 2000
5040 u$ = sv$ + "?a=pub&k=" + ky$ + "&f=" + n$
5050 httpget u$, r$
5060 if left$(r$,2) = "ok" then msg$ = "已发布 " + mid$(r$,4)
5070 if left$(r$,2) <> "ok" then msg$ = "!" + left$(r$,26)
5080 return
