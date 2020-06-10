import * as vscode from 'vscode';
import {ExtensionFunctions} from './extensionFunctions';

export function activate(cobtext: vscode.ExtensionContext) {
    const extensionFunctions = new ExtensionFunctions(vscode.workspace.rootPath);
    vscode.window.registerTreeDataProvider('Servers', extensionFunctions);
    vscode.commands.registerCommand('Servers.refreshEntry', () =>
        extensionFunctions.refresh()
    );
    vscode.commands.registerCommand('Servers.startExtension', () =>
        extensionFunctions.startExtension()
    );
    vscode.commands.registerCommand('Servers.addServer', () => 
        extensionFunctions.addServer()
    );
}