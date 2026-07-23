#!/usr/bin/env bash
# desktop-shell — ai-sdlc 治理 bootstrap
# 用法：在 repo 根目錄解開本包後執行 ./bootstrap.sh /path/to/ai-sdlc-skill
set -euo pipefail

SKILL="${1:-}"
[ -z "$SKILL" ] && { echo "用法: ./bootstrap.sh /path/to/ai-sdlc"; exit 1; }
[ -f AGENTS.md ] || { echo "錯誤：請在 repo 根目錄執行（找不到 AGENTS.md）"; exit 1; }

mkdir -p src tests assets \
         docs/{changes,acceptance,structure,knowledge,worklog} \
         content/{profiles,widgets} apps

cp "$SKILL/scripts/halt_gate.py" scripts/
cp "$SKILL/assets/halt_policy.json" assets/

[ -f docs/knowledge/errors.md ] || cat > docs/knowledge/errors.md <<'EOF'
# 錯誤知識庫
| 日期 | 情境 / 任務 | 錯誤 | 根因 | 解法 | 預防 |
|------|-------------|------|------|------|------|
EOF
[ -f docs/backlog/state.json ] || echo '{"done":[],"in_progress":{}}' > docs/backlog/state.json
for d in docs/worklog docs/changes docs/acceptance docs/structure tests src; do touch "$d/.gitkeep"; done

cat >> .gitignore <<'EOF'

# Qt / QML
build/
*.pro.user*
moc_*.cpp
ui_*.h
qrc_*.cpp
CMakeCache.txt
CMakeFiles/
*.qmlc
*.jsc

# macOS
.DS_Store
EOF

chmod +x scripts/*.py
python3 scripts/plan.py status
echo
echo "bootstrap 完成。接著："
echo "  git add -A && git commit -m 'chore: ai-sdlc governance bootstrap' && git push"
echo "  然後在 GitHub Settings 完成三件事："
echo "    1. Allow auto-merge"
echo "    2. main 的 branch protection，required check = 'ai-sdlc governance / gate'"
echo "    3. 建立標籤 halt:awaiting-human"
