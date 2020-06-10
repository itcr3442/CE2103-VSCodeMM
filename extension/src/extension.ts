import * as fs from "fs-extra";
import * as path from "path";
import * as vscode from "vscode";

class ServerList implements vscode.TreeDataProvider<Server> {
  constructor() {}

  getTreeItem(element: Server): vscode.TreeItem {
    return element;
  }

  getChildren(element?: Server): Thenable<Server[]> {
    return Promise.resolve([]);
  }
}

class Server extends vscode.TreeItem {
  private workspace;
  constructor(
    public readonly label: string,
    private address: string,
    private port: number,
    private secret: string,
    public readonly collapsibleState: vscode.TreeItemCollapsibleState
  ) {
    super(label, collapsibleState);
  }

  iconPath = {
    light: path.join(
      __filename,
      "..",
      "..",
      "resources",
      "light",
      "dependency.svg"
    ),
    dark: path.join(
      __filename,
      "..",
      "..",
      "resources",
      "dark",
      "dependency.svg"
    ),
  };
}

export function activate(cobtext: vscode.ExtensionContext) {
  vscode.window.registerTreeDataProvider("vscodemm.servers", new ServerList());

  vscode.commands.registerCommand("vscodemm.setup", () =>
    fs.copySync("vscodemm/", path.join(vscode.workspace.rootPath, "vscodemm/"))
  );
}
