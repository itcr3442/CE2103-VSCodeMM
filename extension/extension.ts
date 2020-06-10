import * as vscode from 'vscode';
import {ExtensionFunctions} from './extensionFunctions';

export function activate(cobtext: vscode.ExtensionContext) {
    const extensionFunctions = new ExtensionFunctions(vscode.workspace.rootPath);
    vscode.window.registerTreeDataProvider('nodeDependencies', extensionFunctions);
    vscode.commands.registerCommand('nodeDependencies.refreshEntry', () =>
        extensionFunctions.refresh()
    );
    vscode.commands.registerCommand('nodeDependencies.startExtension', () =>
        extensionFunctions.startExtension()
    );
    vscode.commands.registerCommand('nodeDependencies.addServer', () => 
        extensionFunctions.addServer()
    );
}