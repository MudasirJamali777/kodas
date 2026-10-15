// Kodas VSCode extension
import * as vscode from 'vscode';

export function activate(context: vscode.ExtensionContext) {
    console.log('Kodas extension is now active!');

    // Register LSP client (future: spawn kodas lsp and connect)
    context.subscriptions.push(
        vscode.languages.registerHoverProvider('kodas', {
            provideHover(document, position) {
                const range = document.getWordRangeAtPosition(position);
                const word = range ? document.getText(range) : '';
                return new vscode.Hover(`Kodas identifier: \`${word}\``);
            }
        })
    );
}

export function deactivate() {}
