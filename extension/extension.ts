import * as vscode from 'vscode';
import {NodeDependenciesProvider} from './nodeDependencies';

export function activate(cobtext: vscode.ExtensionContext) {
    const nodeDependenciesProvider = new NodeDependenciesProvider(vscode.workspace.rootPath);
    vscode.window.registerTreeDataProvider('nodeDependencies', nodeDependenciesProvider);
    vscode.commands.registerCommand('nodeDependencies.refreshEntry', () =>
        nodeDependenciesProvider.refresh()
    );
    vscode.commands.registerCommand('nodeDependencies.startExtension', () =>
        nodeDependenciesProvider.startExtension()
    );
}