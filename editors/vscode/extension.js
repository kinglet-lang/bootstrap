const vscode = require('vscode');
const { LanguageClient, TransportKind } = require('vscode-languageclient/node');
const os = require('os');

let client;

function activate(context) {
  const config = vscode.workspace.getConfiguration('kinglet');
  const serverPath = config.get('server.path', 'kinglet-lsp');

  const env = Object.assign({}, process.env);
  // Ensure ~/bin is in PATH (macOS GUI apps may not have it)
  env.PATH = `${os.homedir()}/bin:${env.PATH || ''}`;

  const serverOptions = {
    command: serverPath,
    args: [],
    transport: TransportKind.stdio,
    options: { env },
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
