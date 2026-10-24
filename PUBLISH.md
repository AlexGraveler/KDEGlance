# Publishing v1.0.0 (owner action required)

This preparation did not push, create a remote repository/release, or create a
Git tag. The proposed remote is https://github.com/AlexGraveler/KDEGlance.
Remote availability/permissions must be checked by the owner when publishing.

1. Review README, screenshots, LICENSE, THIRD_PARTY, tests and the local validation results.
2. If the GitHub repository already has history, clone/pull that history first;
   copy the prepared source files into that checkout while preserving `.git`.
   Do not overwrite remote history or force-push it.
3. If this is a new repository, initialize the clean source directory:

   ```bash
   git init -b main
   git remote add origin https://github.com/AlexGraveler/KDEGlance.git
   ```

4. Review the staging area before committing. Archives, native binaries and .work
   are ignored. No internal reports or source checksum manifests are published.

   ```bash
   git add .
   git status
   git diff --cached --stat
   git commit -m "Prepare KDEGlance 1.0.0"
   ```

5. Only after final owner approval, create and publish the version tag:

   ```bash
   git tag -a v1.0.0 -m "KDEGlance 1.0.0"
   git push -u origin main
   git push origin v1.0.0
   ```

6. Create a GitHub Release for v1.0.0, paste RELEASE-NOTES.md, and attach the
   minimal user tar.gz and its .sha256 file. The repository includes developer
   material separately; do not attach the old all-in-one source bundle. Verify
   the downloaded artifact checksum.

The owner approved GPL-3.0-or-later during preparation. That license decision
is not permission for this agent to publish or push anything.
