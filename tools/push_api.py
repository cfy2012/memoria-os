#!/usr/bin/env python3
"""Memoria OS 推送工具 —— 走 api.github.com 的 Git Data API。

本机 github.com:443 被网络层重置，git push（HTTPS/SSH）都走不通；
唯一可写通道是 GitHub REST API。token 只从环境变量 GH_PAT 读取，不写盘、不入库。

用法：
    set GH_PAT=ghp_xxx          (PowerShell: $env:GH_PAT='ghp_xxx')
    python tools/push_api.py "G:\\Memoria OS" cfy2012/memoria-os
    python tools/push_api.py "G:\\memoria-ota" cfy2012/memoria-ota

行为：
  * 用「tree 指纹」定位远端已同步到哪个本地提交，逐提交增量重放（保留每个提交的消息与作者）
  * 每个提交都校验建出的 tree 与本地 `commit^{tree}` 是否逐字节相同，不等即中止
  * 全部成功后才移动远端分支（force，因为无签名提交与本地哈希不同）
  * 远端原本没有的提交范围会报错退出，不做猜测
"""

import base64
import json
import os
import subprocess
import sys
import urllib.error
import urllib.request

GIT = os.environ.get("GIT_EXE", r"D:\Git\cmd\git.exe")
API = "https://api.github.com"


def git(repo, *args, binary=True):
    r = subprocess.run([GIT, "-C", repo] + list(args), capture_output=True, timeout=600)
    if r.returncode != 0:
        raise RuntimeError("git %s -> %s" % (args, r.stderr.decode("utf-8", "replace")[:400]))
    return r.stdout if binary else r.stdout.decode("utf-8", "replace").strip()


def api(method, path, token, payload=None):
    data = json.dumps(payload).encode("utf-8") if payload is not None else None
    req = urllib.request.Request(API + path, data=data, method=method, headers={
        "Authorization": "token " + token,
        "Accept": "application/vnd.github+json",
        "User-Agent": "memoria-push",
        "Content-Type": "application/json",
    })
    try:
        with urllib.request.urlopen(req, timeout=180) as resp:
            body = resp.read().decode("utf-8", "replace")
            return json.loads(body) if body.strip() else {}
    except urllib.error.HTTPError as e:
        raise RuntimeError("API %s %s -> %d %s" % (method, path, e.code,
                          e.read().decode("utf-8", "replace")[:700]))


def tree_index(repo, rev):
    idx = {}
    for chunk in git(repo, "ls-tree", "-r", "-z", rev).split(b"\0"):
        if not chunk:
            continue
        head, path = chunk.split(b"\t", 1)
        parts = head.split(b" ")
        idx[path] = (parts[0].decode(), parts[2].decode())
    return idx


def diff_pairs(repo, parent, commit):
    raw = git(repo, "diff", "--name-status", "-z", parent, commit)
    parts = raw.split(b"\0")
    pairs = []
    i = 0
    while i + 1 < len(parts):
        st = parts[i]
        if not st:
            i += 1
            continue
        if st[:1] in (b"R", b"C"):
            pairs.append((st.decode(), parts[i + 1], parts[i + 2]))
            i += 3
        else:
            pairs.append((st.decode(), parts[i + 1], None))
            i += 2
    return pairs


def build_commit(repo, owner, token, commit, parent_remote, parent_tree):
    old_idx = tree_index(repo, commit + "^")
    new_idx = tree_index(repo, commit)
    entries = []
    uploaded = 0
    for st, path, alt in diff_pairs(repo, commit + "^", commit):
        code = st[0]
        if code in ("R", "C"):
            oldpath = path
            path = alt
            if code == "R":
                entries.append({"path": oldpath.decode("utf-8", "surrogateescape"),
                                "mode": old_idx.get(oldpath, ("100644",))[0],
                                "type": "blob", "sha": None})
        pstr = path.decode("utf-8", "surrogateescape")
        if code == "D":
            entries.append({"path": pstr, "mode": old_idx.get(path, ("100644",))[0],
                            "type": "blob", "sha": None})
            continue
        if path not in new_idx:
            raise RuntimeError("本地树里找不到路径：%r" % pstr)
        mode, sha = new_idx[path]
        blob = api("POST", "/repos/%s/git/blobs" % owner, token,
                   {"content": base64.b64encode(git(repo, "cat-file", "blob", sha)).decode(),
                    "encoding": "base64"})
        if blob["sha"] != sha:
            raise RuntimeError("blob 校验失败：%s != %s" % (blob["sha"][:10], sha[:10]))
        uploaded += 1
        entries.append({"path": pstr, "mode": mode, "type": "blob", "sha": sha})

    tree = api("POST", "/repos/%s/git/trees" % owner, token,
               {"base_tree": parent_tree, "tree": entries})
    expect = git(repo, "rev-parse", commit + "^{tree}", binary=False)
    if tree["sha"] != expect:
        raise RuntimeError("tree 不一致：%s != %s（提交 %s）" % (tree["sha"][:10], expect[:10], commit[:10]))
    print("    tree %s  ok  (%d 项, %d blob)" % (tree["sha"][:10], len(entries), uploaded))

    meta = git(repo, "log", "-1", "--pretty=%an%n%ae%n%aI%n%cn%n%ce%n%cI%n%B", commit, binary=False)
    seg = meta.split("\n")
    a_name, a_mail, a_date, c_name, c_mail, c_date = seg[:6]
    msg = "\n".join(seg[6:]).strip()

    c = api("POST", "/repos/%s/git/commits" % owner, token, {
        "message": msg,
        "tree": tree["sha"],
        "parents": [parent_remote],
        "author": {"name": a_name, "email": a_mail, "date": a_date},
        "committer": {"name": c_name, "email": c_mail, "date": c_date},
    })
    print("    commit %s -> %s  %s" % (commit[:10], c["sha"][:10], msg.splitlines()[0][:60]))
    return c["sha"]


def main():
    repo = os.path.abspath(sys.argv[1])
    owner = sys.argv[2]
    branch = sys.argv[3] if len(sys.argv) > 3 else "main"
    token = os.environ.get("GH_PAT", "").strip()
    if not token:
        raise SystemExit("GH_PAT 未设置")

    who = api("GET", "/user", token)
    print("token 用户：%s" % who.get("login"))

    ref = api("GET", "/repos/%s/git/ref/heads/%s" % (owner, branch), token)
    remote_sha = ref["object"]["sha"]
    remote_tree = api("GET", "/repos/%s/git/commits/%s" % (owner, remote_sha), token)["tree"]["sha"]
    local_head = git(repo, "rev-parse", "HEAD", binary=False)
    local_tree = git(repo, "rev-parse", "HEAD^{tree}", binary=False)
    print("远端 %s  %s (tree %s)" % (owner, remote_sha[:10], remote_tree[:10]))
    print("本地 HEAD   %s (tree %s)" % (local_head[:10], local_tree[:10]))

    if remote_tree == local_tree:
        print("已同步，无需推送")
        return

    commits = [c for c in git(repo, "rev-list", "--reverse", "HEAD", binary=False).split("\n") if c.strip()]
    anchor = None
    for c in commits:
        if git(repo, "rev-parse", c + "^{tree}", binary=False) == remote_tree:
            anchor = c
            break
    if anchor is None:
        raise SystemExit("远端树在本地历史里找不到对应提交，需人工核对，已中止")
    todo = commits[commits.index(anchor) + 1:]
    if not todo:
        raise SystemExit("本地没有新提交，但树不一致，已中止")
    print("待推提交 %d 个，基点 %s" % (len(todo), anchor[:10]))

    parent = remote_sha
    ptree = remote_tree
    for c in todo:
        parent = build_commit(repo, owner, token, c, parent, ptree)
        ptree = api("GET", "/repos/%s/git/commits/%s" % (owner, parent), token)["tree"]["sha"]

    api("PATCH", "/repos/%s/git/refs/heads/%s" % (owner, branch), token,
        {"sha": parent, "force": True})
    v = api("GET", "/repos/%s/git/ref/heads/%s" % (owner, branch), token)
    final_tree = api("GET", "/repos/%s/git/commits/%s" % (owner, parent), token)["tree"]["sha"]
    print("远端 %s 现在 = %s，tree 校验 %s" %
          (branch, v["object"]["sha"][:10], "通过" if final_tree == local_tree else "失败"))
    if final_tree != local_tree:
        raise SystemExit(2)


if __name__ == "__main__":
    main()
