import * as vscode from 'vscode';
import * as fs from 'fs-extra';
import * as path from 'path';
import { promises } from 'fs';

export class ExtensionFunctions implements vscode.TreeDataProvider<Dependency> {

    private _onDidChangeTreeData: vscode.EventEmitter<Dependency | undefined> = new vscode.EventEmitter<Dependency | undefined>();
	readonly onDidChangeTreeData: vscode.Event<Dependency | undefined> = this._onDidChangeTreeData.event;

    constructor(private workspaceRoot: string) {}

    callback(err): void {
        if (err) throw err;
    }
    c: string;
    startExtension(): void{
        fs.copy('vscodemm', this.workspaceRoot, this.callback);
        vscode.window.showInformationMessage('Extensión iniciada');
        vscode.window.showInformationMessage(""+this.c);
    }

    refresh(): void {
        this._onDidChangeTreeData.fire(undefined);
    }
    
    async addServer(): Promise<any>{
        this.c = await vscode.window.showInputBox();
    }

    getTreeItem(element: Dependency): vscode.TreeItem {
        return element;
    }

    getChildren(element?: Dependency): Thenable<Dependency[]> {
        if (!this.workspaceRoot) {
            vscode.window.showInformationMessage('No dependency in empty workspace at '+this.workspaceRoot);
            return Promise.resolve([]);
        }

        if (element) {
            return Promise.resolve(
                this.getDepsInPackageJson(
                    path.join(this.workspaceRoot, 'node_modules', element.label, 'package.json')
                )
            );
        } else {
            const packageJsonPath = path.join(this.workspaceRoot, 'package.json');
            if (this.pathExists(packageJsonPath)) {
                return Promise.resolve(this.getDepsInPackageJson(packageJsonPath));
            } else {
                vscode.window.showInformationMessage('Workspace has no package.json');
                return Promise.resolve([]);
            }
        }
    }

    private getDepsInPackageJson(packageJsonPath: string): Dependency[]{
        if (this.pathExists(packageJsonPath)) {
            const packageJson = JSON.parse(fs.readFileSync(packageJsonPath, 'utf-8'));
        
            const toDep = (moduleName: string, version: string): Dependency => {
                if (this.pathExists(path.join(this.workspaceRoot, 'node_modules', moduleName))) {
                    return new Dependency(
                        moduleName,
                        version,
                        vscode.TreeItemCollapsibleState.Collapsed
                    );
                } else {
                    return new Dependency (moduleName, version, vscode.TreeItemCollapsibleState.None);
                }
            };

            const deps = packageJson.dependencies
                ? Object.keys(packageJson.dependencies).map(dep =>
                        toDep(dep, packageJson.devDependencies[dep])
                    )
                : [];

            const devDeps = packageJson.devDependencies
                ? Object.keys(packageJson.devDependencies).map(dep =>
                        toDep(dep, packageJson.devDependencies[dep])
                    )
                : [];

            return deps.concat(devDeps);
                
    
        } else {
            return [];
        }
    }

    private pathExists(p: string): boolean {
        try {
            fs.accessSync(p);
        } catch (err) {
            return false;
        }
        return true;
    }
}

class Dependency extends vscode.TreeItem {
    constructor(
        public readonly label: string,
        private version: string,
        public readonly collapsibleState: vscode.TreeItemCollapsibleState
    ) {
        super (label, collapsibleState);
    }

    get tooltip(): string{
        return `$(this.label)-$(this.version)`;
    }

    get description(): string{
        return this.version;
    }

    iconPath = {
        light: path.join(__filename, '..', '..', 'resources', 'light', 'dependency.svg'),
        dark: path.join(__filename, '..', '..', 'resources', 'dark', 'dependency.svg')
    };
}