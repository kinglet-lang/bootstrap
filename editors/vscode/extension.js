const vscode = require('vscode');
const { LanguageClient, TransportKind } = require('vscode-languageclient/node');
const os = require('os');

let client;

function activate(context) {
  const serverOptions = {
    command: 'kinglet-lsp',
    transport: TransportKind.stdio,
    options: {
      env: Object.assign({}, process.env, {
        PATH: `${os.homedir()}/bin:${process.env.PATH || ''}`
      })
    }
  };

  const clientOptions = {
    documentSelector: [{ scheme: 'file', language: 'kinglet' }],
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
  if (client) return client.stop();
}

module.exports = { activate, deactivate };
