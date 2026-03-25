#!/usr/bin/env bash

set -e

REPO="bharadwajsanket/ion"

echo "🚀 Installing ion..."

OS="$(uname -s)"

if [[ "$OS" == "Darwin" ]]; then
    FILE="ion-mac"
elif [[ "$OS" == "Linux" ]]; then
    FILE="ion-linux"
else
    echo "❌ Unsupported OS: $OS"
    exit 1
fi

LATEST=$(curl -s https://api.github.com/repos/$REPO/releases/latest | grep '"tag_name"' | cut -d '"' -f 4)

if [[ -z "$LATEST" ]]; then
    echo "❌ Failed to fetch latest version"
    exit 1
fi

echo "📦 Latest version: $LATEST"

URL="https://github.com/$REPO/releases/download/$LATEST/$FILE"

echo "⬇️ Downloading..."
curl -L "$URL" -o ion

chmod +x ion
sudo mv ion /usr/local/bin/ion

echo "✅ Installed successfully!"
echo "👉 Run: ion help"