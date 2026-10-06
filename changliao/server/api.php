<?php
// @file api.php @brief 畅聊服务端 v2：纯 PHP+MySQL，HTTP 轮询，纯文本行协议（面向 BASIC 客户端）
// v2 变更（2026-10-06 豆包-2）：
//   - 会话令牌：login 返回 token，后续请求带 t= 认证，密码只出现在登录那一次（POST body）
//   - send/reg/login 支持 POST body（内容/密码不再明文进 URL），保留 GET 兼容
//   - 房间真正落地：cl_msgs 加 room 列，send/get 按房间隔离
//   - 新增 rooms（房间列表）/ online（在线名单，get 轮询即心跳）
// 协议：响应 text/plain，行分隔，字段用 "|" 分隔（服务端滤掉内容中的 "|"）
// 接口：
//   GET  ?a=ping                                     → pong changliao v2
//   POST ?a=reg&u=名  body=密码                       → ok / err taken / err bad
//   POST ?a=login&u=名  body=密码                     → ok <token> / err
//   POST ?a=send&u&t&room=名  body=内容（≤max_msg_bytes）→ ok <mid> / err auth / err long
//   GET  ?a=get&u&t&room=名&after=<mid>               → 每行 mid|sender|mtype|body / none（心跳）
//   POST ?a=voice&u&t&room=名  body=WAV               → ok data/v<mid>.wav / err
//   GET  ?a=rooms&u&t                                → 每行房间名 / none
//   GET  ?a=online&u&t&room=名                       → 每行在线用户名 / none
// 兼容说明：老 GET 写法（reg/login 的 p=、send 的 m= 与 p=）仍可用，v2 客户端走 POST + token。

error_reporting(0);
ini_set('display_errors', '0');
header("Content-Type: text/plain; charset=utf-8");
header("Cache-Control: no-store");

require __DIR__ . "/config.php";

function out($s) { echo $s; exit; }

// ---- 数据库 ----
$db = @mysqli_connect($db_host, $db_user, $db_pass, $db_name);
if (!$db) out("err db");
mysqli_set_charset($db, "utf8");

// 自动建表（幂等，免安装步骤）
mysqli_query($db, "CREATE TABLE IF NOT EXISTS cl_users (uid INT AUTO_INCREMENT PRIMARY KEY, uname VARCHAR(32) NOT NULL UNIQUE, upass CHAR(32) NOT NULL, created INT NOT NULL) ENGINE=MyISAM DEFAULT CHARSET=utf8");
mysqli_query($db, "CREATE TABLE IF NOT EXISTS cl_msgs (mid INT AUTO_INCREMENT PRIMARY KEY, sender VARCHAR(32) NOT NULL, mtype TINYINT NOT NULL, body TEXT NOT NULL, room VARCHAR(32) NOT NULL DEFAULT 'lobby', ts INT NOT NULL) ENGINE=MyISAM DEFAULT CHARSET=utf8");

// ---- 幂等迁移：老库补列 ----
function ensure_col($db, $tbl, $col, $def) {
  $r = mysqli_query($db, "SHOW COLUMNS FROM " . $tbl . " LIKE '" . $col . "'");
  if ($r && mysqli_num_rows($r) === 0) {
    mysqli_query($db, "ALTER TABLE " . $tbl . " ADD " . $col . " " . $def);
  }
}
ensure_col($db, "cl_msgs", "room", "VARCHAR(32) NOT NULL DEFAULT 'lobby' AFTER body");
ensure_col($db, "cl_users", "last_active", "INT NOT NULL DEFAULT 0");
ensure_col($db, "cl_users", "token", "CHAR(32) NOT NULL DEFAULT ''");

// ---- 工具 ----
function q($db, $s) { return mysqli_real_escape_string($db, $s); }
function g($k, $d) { return isset($_GET[$k]) ? $_GET[$k] : $d; }
function gb($k, $d) { return isset($_POST[$k]) ? $_POST[$k] : (isset($_GET[$k]) ? $_GET[$k] : $d); }

function raw_body() {
  $b = file_get_contents("php://input");
  return $b === false ? "" : $b;
}

function clean_body($s) {
  // 协议安全：滤分隔符与换行
  $s = str_replace("|", "/", $s);
  $s = str_replace(array("\r", "\n"), " ", $s);
  return $s;
}

function clean_room($s) {
  $s = trim(str_replace(array("|", "\r", "\n", " "), "", $s));
  if ($s === "") return "lobby";
  return substr($s, 0, 32);
}

// 认证：v2 = u+t（token）；兼容 = u+p
function auth($db) {
  $u = trim(gb("u", ""));
  if (!preg_match('/^[a-zA-Z0-9_]{2,20}$/', $u)) return false;
  $t = gb("t", "");
  if ($t !== "") {
    if (!preg_match('/^[a-f0-9]{32}$/', $t)) return false;
    $r = mysqli_query($GLOBALS["db"], "SELECT uid FROM cl_users WHERE uname='" . q($GLOBALS["db"], $u) . "' AND token='" . q($GLOBALS["db"], $t) . "' LIMIT 1");
  } else {
    $p = gb("p", "");
    if ($p === "" && !isset($_POST["p"]) && !isset($_GET["p"])) $p = raw_body();
    if (strlen($p) === 0) return false;
    $r = mysqli_query($GLOBALS["db"], "SELECT uid FROM cl_users WHERE uname='" . q($GLOBALS["db"], $u) . "' AND upass='" . md5($p) . "' LIMIT 1");
  }
  if (!$r || mysqli_num_rows($r) !== 1) return false;
  mysqli_query($GLOBALS["db"], "UPDATE cl_users SET last_active=" . time() . " WHERE uname='" . q($GLOBALS["db"], $u) . "'");
  return true;
}

$a = g("a", "");

// ---- 部署验活 ----
if ($a === "ping") out("pong changliao v2");

// ---- 注册（POST body=密码）----
if ($a === "reg") {
  $u = trim(gb("u", "")); $p = gb("p", "");
  if ($p === "" && !isset($_POST["p"]) && !isset($_GET["p"])) $p = raw_body();
  if (!preg_match('/^[a-zA-Z0-9_]{2,20}$/', $u) || strlen($p) < 4 || strlen($p) > 32) out("err bad");
  $r = mysqli_query($db, "SELECT uid FROM cl_users WHERE uname='" . q($db, $u) . "' LIMIT 1");
  if ($r && mysqli_num_rows($r) > 0) out("err taken");
  mysqli_query($db, "INSERT INTO cl_users (uname, upass, created, last_active, token) VALUES ('" . q($db, $u) . "', '" . md5($p) . "', " . time() . ", " . time() . ", '')");
  out(mysqli_errno($db) ? "err db" : "ok");
}

// ---- 登录（POST body=密码，返回会话令牌）----
if ($a === "login") {
  $u = trim(gb("u", ""));
  $p = gb("p", "");
  if ($p === "" && !isset($_POST["p"]) && !isset($_GET["p"])) $p = raw_body();
  if (!preg_match('/^[a-zA-Z0-9_]{2,20}$/', $u) || strlen($p) === 0) out("err");
  $r = mysqli_query($db, "SELECT uid FROM cl_users WHERE uname='" . q($db, $u) . "' AND upass='" . md5($p) . "' LIMIT 1");
  if (!$r || mysqli_num_rows($r) !== 1) out("err");
  $tok = md5($u . time() . mt_rand());
  mysqli_query($db, "UPDATE cl_users SET token='" . $tok . "', last_active=" . time() . " WHERE uname='" . q($db, $u) . "'");
  out("ok " . $tok);
}

// ---- 发文字（POST body=内容）----
if ($a === "send") {
  if (!auth($db)) out("err auth");
  $u = trim(gb("u"));
  $room = clean_room(gb("room", "lobby"));
  $m = gb("m", "");
  if ($m === "" && !isset($_POST["m"]) && !isset($_GET["m"])) $m = raw_body();
  $m = clean_body($m);
  if ($m === "") out("err bad");
  if (strlen($m) > $max_msg_bytes) out("err long");
  mysqli_query($db, "INSERT INTO cl_msgs (sender, mtype, body, room, ts) VALUES ('" . q($db, $u) . "', 0, '" . q($db, $m) . "', '" . q($db, $room) . "', " . time() . ")");
  out(mysqli_errno($db) ? "err db" : "ok " . mysqli_insert_id($db));
}

// ---- 拉消息（轮询，按房间，after 分页，心跳）----
if ($a === "get") {
  if (!auth($db)) out("err auth");
  $room = clean_room(gb("room", "lobby"));
  $after = intval(gb("after", 0));
  $r = mysqli_query($db, "SELECT mid, sender, mtype, body FROM cl_msgs WHERE room='" . q($db, $room) . "' AND mid > " . $after . " ORDER BY mid ASC LIMIT " . intval($max_fetch_rows));
  if (!$r) out("err db");
  $n = 0;
  while ($row = mysqli_fetch_row($r)) {
    echo ($n ? "\n" : "") . $row[0] . "|" . $row[1] . "|" . $row[2] . "|" . $row[3];
    $n++;
  }
  if (!$n) out("none");
  exit;
}

// ---- 房间列表 ----
if ($a === "rooms") {
  if (!auth($db)) out("err auth");
  $r = mysqli_query($db, "SELECT DISTINCT room FROM cl_msgs ORDER BY room ASC");
  if (!$r) out("err db");
  $n = 0;
  while ($row = mysqli_fetch_row($r)) {
    echo ($n ? "\n" : "") . $row[0];
    $n++;
  }
  if (!$n) out("none");
  exit;
}

// ---- 在线名单（最近 60 秒有动作 = 在线，限本房间最近发言者）----
if ($a === "online") {
  if (!auth($db)) out("err auth");
  $room = clean_room(gb("room", "lobby"));
  $cut = time() - 60;
  $r = mysqli_query($db, "SELECT DISTINCT m.sender FROM cl_msgs m WHERE m.room='" . q($db, $room) . "' AND m.sender IN (SELECT uname FROM cl_users WHERE last_active > " . $cut . ") ORDER BY m.sender ASC");
  if (!$r) out("err db");
  $n = 0;
  while ($row = mysqli_fetch_row($r)) {
    echo ($n ? "\n" : "") . $row[0];
    $n++;
  }
  if (!$n) out("none");
  exit;
}

// ---- 发语音（POST body = WAV 原始字节，带房间）----
if ($a === "voice") {
  if (!auth($db)) out("err auth");
  $u = trim(gb("u"));
  $room = clean_room(gb("room", "lobby"));
  $bin = raw_body();
  if ($bin === false || strlen($bin) < $min_voice_bytes) out("err bad");
  if (strlen($bin) > $max_voice_bytes) out("err big");
  if (substr($bin, 0, 4) !== "RIFF") out("err wav");
  if (!is_dir($voice_dir)) { @mkdir($voice_dir, 0755, true); }
  mysqli_query($db, "INSERT INTO cl_msgs (sender, mtype, body, room, ts) VALUES ('" . q($db, $u) . "', 1, '', '" . q($db, $room) . "', " . time() . ")");
  $mid = mysqli_insert_id($db);
  if (!$mid) out("err db");
  $name = "v" . $mid . ".wav";
  if (@file_put_contents($voice_dir . "/" . $name, $bin) === false) {
    mysqli_query($db, "DELETE FROM cl_msgs WHERE mid=" . $mid);
    out("err io");
  }
  mysqli_query($db, "UPDATE cl_msgs SET body='" . q($db, $voice_url_base . "/" . $name) . "' WHERE mid=" . $mid);
  out("ok " . $voice_url_base . "/" . $name);
}

out("err action");
