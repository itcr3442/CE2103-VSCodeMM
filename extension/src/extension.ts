import * as fs from "fs-extra";
import * as path from "path";
import * as vscode from "vscode";

function setContext(key: string, value: boolean): void {
  vscode.commands.executeCommand("setContext", key, value);
}

interface SavedServer {
  name: string;
  address: string;
  secret: string;
}

class WorkspaceState {
  private initialized: boolean = false;
  private servers: SavedServer[] = [];

  public constructor() {
    const base = this.getPrivateDirectoryPath();
    if (!fs.existsSync(base)) {
      fs.mkdirSync(base);
    }

    if (!this.load()) {
      this.save();
    }
  }

  public setInitialized(initialized: boolean): void {
    this.initialized = initialized;
    setContext("vscodemm.initialized", initialized);
  }

  public save(): void {
    let contents = JSON.stringify({
      initialized: this.initialized,
      servers: this.servers,
    });
    fs.writeFileSync(this.getPath(), contents);
  }

  public getServers(): Server[] {
    let servers = [];
    for (let server of this.servers) {
      servers.push(new Server(server.name, server.address, server.secret));
    }

    return servers;
  }

  private load(): boolean {
    const path = this.getPath();
    if (!fs.existsSync(path)) {
      return false;
    }

    const state = JSON.parse(fs.readFileSync(path, "utf-8"));
    this.setInitialized(state.initialized);
    this.servers = state.servers;

    return true;
  }

  private getPath(): string {
    return path.join(this.getPrivateDirectoryPath(), "vscodemm.json");
  }

  private getPrivateDirectoryPath(): string {
    return path.join(vscode.workspace.rootPath, ".vscode");
  }
}

let currentState = new WorkspaceState();

class ServerList implements vscode.TreeDataProvider<Server> {
  constructor() {}

  getTreeItem(element: Server): vscode.TreeItem {
    return element;
  }

  getChildren(element?: Server): Thenable<Server[]> {
    if (element === undefined) {
      return Promise.resolve(currentState.getServers());
    }

    return Promise.resolve([]);
  }
}

class Server extends vscode.TreeItem {
  private workspace;
  constructor(
    public readonly name: string,
    private address: string,
    private secret: string
  ) {
    super(name, vscode.TreeItemCollapsibleState.None);
  }

  get tooltip(): string {
    return this.label;
  }

  get description(): string {
    return this.address;
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

  vscode.commands.registerCommand("vscodemm.setup", () => {
    fs.copySync("vscodemm/", path.join(vscode.workspace.rootPath, "vscodemm/"));

    currentState.setInitialized(true);
    currentState.save();
  });

  setContext("vscodemm.running", false);
}
