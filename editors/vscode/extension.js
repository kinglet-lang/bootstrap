const vscode = require('vscode');
const { LanguageClient, TransportKind } = require('vscode-languageclient/node');
const fs = require('fs');
const path = require('path');

function resolveServerPath() {
  const config = vscode.workspace.getConfiguration('kinglet');
  const configured = config.get('server.path', '');
  if (configured) return configured;

  const candidates = [
    path.join(vscode.workspace.rootPath || '', 'out', 'Debug', 'kinglet-lsp'),
    path.join(vscode.workspace.rootPath || '', 'out', 'Release', 'kinglet-lsp'),
    path.join(vscode.workspace.rootPath || '', 'out', 'Default', 'kinglet-lsp'),
  ];
  for (const c of candidates) {
    if (fs.existsSync(c)) return c;
  }
  return 'kinglet-lsp';
}

let client;

function activate(context) {
  const serverOptions = {
    command: resolveServerPath(),
    transport: TransportKind.stdio,
  };

  const clientOptions = {
    documentSelector: [{ scheme: 'file', language: 'kinglet' }],
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher('**/*.kl')
    }
  };

  client = new LanguageClient(
    'kinglet',
    'Kinglet Language Server',
    serverOptions,
    clientOptions
  );

  context.subscriptions.push(client.start());
}

function deactivate() {
  if (client) {
    return client.stop();
  }
}

module.exports = { activate, deactivate };
