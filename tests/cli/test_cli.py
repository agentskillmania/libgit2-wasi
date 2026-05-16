#!/usr/bin/env python3
"""
CLI integration tests for git2.

Tests verify git2 behavior against system git where applicable,
with fuzzy matching for SHAs, timestamps, and other volatile output.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


GIT2 = os.environ.get("GIT2_BIN", os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "build-native", "git2")))
GIT = "/usr/bin/git"


def run_git2(*args, cwd=None):
    """Run git2 command, return (rc, stdout, stderr)."""
    cmd = [GIT2] + list(args)
    result = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
    return result.returncode, result.stdout, result.stderr


def run_git(*args, cwd=None):
    """Run system git command, return (rc, stdout, stderr)."""
    cmd = [GIT] + list(args)
    result = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
    return result.returncode, result.stdout, result.stderr


def normalize_output(text):
    """Normalize volatile parts of git output for comparison."""
    # Replace full SHAs with short SHA placeholder
    text = re.sub(r'\b[a-f0-9]{40}\b', '<SHA>', text)
    # Replace short SHAs (7-20 hex chars)
    text = re.sub(r'\b[a-f0-9]{7,20}\b', '<SHORT_SHA>', text)
    # Replace timestamps
    text = re.sub(r'\d{10}\s+[+-]\d{4}', '<DATE>', text)
    text = re.sub(r'\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\s+[+-]\d{4}', '<DATE>', text)
    # Replace branch names in "Switched to branch 'x'" messages
    text = re.sub(r"Switched to branch '[^']+'", "Switched to branch '<BRANCH>'", text)
    # Replace "HEAD is now at" messages
    text = re.sub(r"HEAD is now at [a-f0-9]+", "HEAD is now at <SHORT_SHA>", text)
    return text


def setup_repo(path):
    """Initialize a git repo with a commit."""
    os.makedirs(path, exist_ok=True)
    run_git("init", cwd=path)
    with open(os.path.join(path, "file.txt"), "w") as f:
        f.write("hello\n")
    run_git("add", "file.txt", cwd=path)
    run_git("commit", "-m", "initial", cwd=path)
    return path


class TestAdd(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.repo = setup_repo(self.tmpdir)

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def test_add_file(self):
        with open(os.path.join(self.repo, "new.txt"), "w") as f:
            f.write("new\n")
        rc, _, _ = run_git2("add", "new.txt", cwd=self.repo)
        self.assertEqual(rc, 0)
        rc, out, _ = run_git("status", "--short", cwd=self.repo)
        self.assertIn("A  new.txt", out)

    def test_add_all(self):
        with open(os.path.join(self.repo, "a.txt"), "w") as f:
            f.write("a\n")
        with open(os.path.join(self.repo, "b.txt"), "w") as f:
            f.write("b\n")
        rc, _, _ = run_git2("add", ".", cwd=self.repo)
        self.assertEqual(rc, 0)
        rc, out, _ = run_git("status", "--short", cwd=self.repo)
        self.assertIn("A  a.txt", out)
        self.assertIn("A  b.txt", out)


class TestDiff(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.repo = setup_repo(self.tmpdir)
        with open(os.path.join(self.repo, "file.txt"), "a") as f:
            f.write("world\n")
        run_git("add", "file.txt", cwd=self.repo)

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def test_diff_staged(self):
        rc, out, _ = run_git2("diff", "--staged", cwd=self.repo)
        self.assertEqual(rc, 0)
        self.assertIn("+world", out)

    def test_diff_cached_alias(self):
        rc1, out1, _ = run_git2("diff", "--staged", cwd=self.repo)
        rc2, out2, _ = run_git2("diff", "--cached", cwd=self.repo)
        self.assertEqual(rc1, 0)
        self.assertEqual(rc2, 0)
        self.assertEqual(out1, out2)


class TestCommit(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.repo = setup_repo(self.tmpdir)
        with open(os.path.join(self.repo, "file.txt"), "a") as f:
            f.write("world\n")
        run_git("add", "file.txt", cwd=self.repo)

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def test_commit(self):
        rc, out, _ = run_git2("commit", "-m", "second", cwd=self.repo)
        self.assertEqual(rc, 0)
        self.assertIn("second", out)

    def test_commit_amend_reuse_message(self):
        run_git("commit", "-m", "second", cwd=self.repo)
        rc, out, _ = run_git2("commit", "--amend", cwd=self.repo)
        self.assertEqual(rc, 0)
        rc, out, _ = run_git("log", "-1", "--pretty=%B", cwd=self.repo)
        self.assertIn("second", out)


class TestLog(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.repo = setup_repo(self.tmpdir)
        with open(os.path.join(self.repo, "file.txt"), "a") as f:
            f.write("world\n")
        run_git("add", "file.txt", cwd=self.repo)
        run_git("commit", "-m", "second", cwd=self.repo)

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def test_log_oneline(self):
        rc, out, _ = run_git2("log", "--oneline", cwd=self.repo)
        self.assertEqual(rc, 0)
        lines = [l for l in out.strip().split("\n") if l.strip()]
        self.assertEqual(len(lines), 2)
        self.assertIn("second", lines[0])
        self.assertIn("initial", lines[1])

    def test_log_range(self):
        rc, out, _ = run_git2("log", "HEAD~1..HEAD", cwd=self.repo)
        self.assertEqual(rc, 0)
        self.assertIn("second", out)
        self.assertNotIn("initial", out)

    def test_log_has_committer(self):
        rc, out, _ = run_git2("log", "-n", "1", cwd=self.repo)
        self.assertEqual(rc, 0)
        self.assertIn("Committer:", out)


class TestBranch(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.repo = setup_repo(self.tmpdir)

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def test_branch_rename(self):
        rc, _, _ = run_git2("branch", "-m", "main", "master", cwd=self.repo)
        self.assertEqual(rc, 0)
        rc, out, _ = run_git("branch", cwd=self.repo)
        self.assertIn("master", out)
        self.assertNotIn("main", out)

    def test_branch_rename_current(self):
        rc, _, _ = run_git2("branch", "-M", "master", cwd=self.repo)
        self.assertEqual(rc, 0)
        rc, out, _ = run_git("branch", cwd=self.repo)
        self.assertIn("master", out)


class TestTag(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.repo = setup_repo(self.tmpdir)

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def test_tag_list(self):
        run_git("tag", "v1.0", cwd=self.repo)
        rc, out, _ = run_git2("tag", cwd=self.repo)
        self.assertEqual(rc, 0)
        self.assertIn("v1.0", out)

    def test_tag_annotated(self):
        rc, _, _ = run_git2("tag", "-a", "v1.0", "-m", "release 1.0", cwd=self.repo)
        self.assertEqual(rc, 0)
        rc, out, _ = run_git("tag", "-n1", cwd=self.repo)
        self.assertIn("release 1.0", out)

    def test_tag_delete(self):
        run_git("tag", "v1.0", cwd=self.repo)
        rc, _, _ = run_git2("tag", "-d", "v1.0", cwd=self.repo)
        self.assertEqual(rc, 0)
        rc, out, _ = run_git("tag", cwd=self.repo)
        self.assertNotIn("v1.0", out)


class TestShow(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.repo = setup_repo(self.tmpdir)

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def test_show_commit(self):
        rc, out, _ = run_git2("show", "HEAD", cwd=self.repo)
        self.assertEqual(rc, 0)
        self.assertIn("commit", out)
        self.assertIn("initial", out)

    def test_show_blob(self):
        rc, out, _ = run_git2("show", "HEAD:file.txt", cwd=self.repo)
        self.assertEqual(rc, 0)
        self.assertIn("hello", out)

    def test_show_tree(self):
        rc, out, _ = run_git2("show", "HEAD^{tree}", cwd=self.repo)
        self.assertEqual(rc, 0)
        self.assertIn("file.txt", out)

    def test_show_tag(self):
        run_git("tag", "-a", "v1.0", "-m", "release", cwd=self.repo)
        rc, out, _ = run_git2("show", "v1.0", cwd=self.repo)
        self.assertEqual(rc, 0)
        self.assertIn("tag v1.0", out)
        self.assertIn("release", out)


class TestStash(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.repo = setup_repo(self.tmpdir)

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def test_stash_push_and_pop(self):
        with open(os.path.join(self.repo, "file.txt"), "a") as f:
            f.write("stashme\n")
        rc, _, _ = run_git2("stash", cwd=self.repo)
        self.assertEqual(rc, 0)
        rc, out, _ = run_git2("stash", "list", cwd=self.repo)
        self.assertEqual(rc, 0)
        self.assertTrue(len(out.strip()) > 0)
        rc, _, _ = run_git2("stash", "pop", cwd=self.repo)
        self.assertEqual(rc, 0)
        with open(os.path.join(self.repo, "file.txt")) as f:
            self.assertIn("stashme", f.read())

    def test_stash_message(self):
        with open(os.path.join(self.repo, "file.txt"), "a") as f:
            f.write("stashme\n")
        rc, _, _ = run_git2("stash", "-m", "custom msg", cwd=self.repo)
        self.assertEqual(rc, 0)
        rc, out, _ = run_git2("stash", "list", cwd=self.repo)
        self.assertIn("custom msg", out)


class TestCheckout(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.repo = setup_repo(self.tmpdir)
        with open(os.path.join(self.repo, "file.txt"), "a") as f:
            f.write("world\n")
        run_git("add", "file.txt", cwd=self.repo)
        run_git("commit", "-m", "second", cwd=self.repo)

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def test_checkout_branch(self):
        run_git("branch", "develop", cwd=self.repo)
        rc, _, _ = run_git2("checkout", "develop", cwd=self.repo)
        self.assertEqual(rc, 0)
        rc, out, _ = run_git("branch", "--show-current", cwd=self.repo)
        self.assertIn("develop", out)

    def test_checkout_b(self):
        rc, _, _ = run_git2("checkout", "-b", "feature", cwd=self.repo)
        self.assertEqual(rc, 0)
        rc, out, _ = run_git("branch", "--show-current", cwd=self.repo)
        self.assertIn("feature", out)

    def test_checkout_B(self):
        rc, _, _ = run_git2("checkout", "-B", "feature", cwd=self.repo)
        self.assertEqual(rc, 0)
        rc, out, _ = run_git("branch", "--show-current", cwd=self.repo)
        self.assertIn("feature", out)

    def test_checkout_file(self):
        with open(os.path.join(self.repo, "file.txt"), "w") as f:
            f.write("changed\n")
        rc, _, _ = run_git2("checkout", "--", "file.txt", cwd=self.repo)
        self.assertEqual(rc, 0)
        with open(os.path.join(self.repo, "file.txt")) as f:
            self.assertEqual(f.read().strip(), "hello\nworld")


class TestReset(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.repo = setup_repo(self.tmpdir)
        with open(os.path.join(self.repo, "file.txt"), "a") as f:
            f.write("world\n")
        run_git("add", "file.txt", cwd=self.repo)

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def test_reset_path(self):
        rc, _, _ = run_git2("reset", "HEAD", "--", "file.txt", cwd=self.repo)
        self.assertEqual(rc, 0)
        rc, out, _ = run_git("status", "--short", cwd=self.repo)
        self.assertIn(" M file.txt", out)

    def test_reset_soft(self):
        run_git("commit", "-m", "second", cwd=self.repo)
        rc, _, _ = run_git2("reset", "--soft", "HEAD~1", cwd=self.repo)
        self.assertEqual(rc, 0)
        rc, out, _ = run_git("status", "--short", cwd=self.repo)
        self.assertIn("M  file.txt", out)


class TestStatus(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.repo = setup_repo(self.tmpdir)

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def test_status_basic(self):
        with open(os.path.join(self.repo, "file.txt"), "a") as f:
            f.write("world\n")
        rc, out, _ = run_git2("status", cwd=self.repo)
        self.assertEqual(rc, 0)
        self.assertIn("modified:   file.txt", out)


class TestPull(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.origin = os.path.join(self.tmpdir, "origin")
        self.clone = os.path.join(self.tmpdir, "clone")
        setup_repo(self.origin)
        run_git("clone", self.origin, self.clone, cwd=self.tmpdir)
        with open(os.path.join(self.origin, "new.txt"), "w") as f:
            f.write("new\n")
        run_git("add", "new.txt", cwd=self.origin)
        run_git("commit", "-m", "upstream", cwd=self.origin)

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def test_pull_ff(self):
        rc, out, _ = run_git2("pull", cwd=self.clone)
        self.assertEqual(rc, 0)
        self.assertIn("Fast-forward", out)
        self.assertTrue(os.path.exists(os.path.join(self.clone, "new.txt")))


class TestPush(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.origin = os.path.join(self.tmpdir, "origin")
        self.clone = os.path.join(self.tmpdir, "clone")
        os.makedirs(self.origin, exist_ok=True)
        run_git("init", "--bare", cwd=self.origin)
        run_git("clone", self.origin, self.clone, cwd=self.tmpdir)
        with open(os.path.join(self.clone, "new.txt"), "w") as f:
            f.write("new\n")
        run_git("add", "new.txt", cwd=self.clone)
        run_git("commit", "-m", "local", cwd=self.clone)

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def test_push(self):
        rc, out, _ = run_git2("push", "origin", "main", cwd=self.clone)
        self.assertEqual(rc, 0)
        self.assertTrue(len(out.strip()) > 0)

    def test_push_set_upstream(self):
        rc, out, _ = run_git2("push", "-u", "origin", "main", cwd=self.clone)
        self.assertEqual(rc, 0)
        rc, out, _ = run_git("rev-parse", "--abbrev-ref", "main@{upstream}", cwd=self.clone)
        self.assertIn("origin/main", out)


if __name__ == "__main__":
    unittest.main(verbosity=2)
