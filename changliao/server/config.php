<?php
// @file config.php @brief 畅聊服务端配置（仓库版，无敏感信息）
// 敏感凭据来源（优先级高→低）：
//   1. 环境变量 CL_DB_HOST / CL_DB_USER / CL_DB_PASS / CL_DB_NAME
//   2. config.local.php（同目录，不入仓库，模板见 config.local.example.php）
// 凭据未配置时 $db_user/$db_pass 为空串，api.php 连库显式返回 "err db"，无默认密码回退。

// MySQL 连接（easypanel 常规：库名=用户名=面板账号，host=localhost）
$db_host = "localhost";
$db_user = "";
$db_pass = "";
$db_name = "";

foreach (array("CL_DB_HOST" => "db_host", "CL_DB_USER" => "db_user",
               "CL_DB_PASS" => "db_pass", "CL_DB_NAME" => "db_name") as $env_k => $var_n) {
    $env_v = getenv($env_k);
    if ($env_v !== false && $env_v !== "") { $$var_n = $env_v; }
}
if (is_file(__DIR__ . "/config.local.php")) { require __DIR__ . "/config.local.php"; }

// 语音存储
$voice_dir       = __DIR__ . "/data";  // WAV 落盘目录（自动创建）
$voice_url_base  = "data";             // 相对 api.php 的访问前缀
$max_voice_bytes = 524288;             // 单条语音上限 512KB
$min_voice_bytes = 44;                 // WAV 头最小长度

// 消息限制
$max_msg_bytes   = 200;                // 单条文字上限（字节）
$max_fetch_rows  = 30;                 // 单次拉取上限（保证响应 <8KB）
