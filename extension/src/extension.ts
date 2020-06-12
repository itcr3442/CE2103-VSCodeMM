import * as fs from "fs-extra";
import * as net from "net";
import * as path from "path";
import * as vscode from "vscode";

function setContext(key: string, value: boolean): void {
  vscode.commands.executeCommand("setContext", key, value);
}

function setRunning(running: boolean) {
  setContext("vscodemm.running", running);
}

class Server {
  public constructor(
    readonly name: string,
    readonly address: string,
    readonly port: number,
    readonly secret: string
  ) {}
}

class WorkspaceState {
  private initialized: boolean = false;
  private servers: Server[] = [];

  public constructor() {
    const base = this.getPrivateDirectoryPath();
    if (!fs.existsSync(base)) {
      fs.mkdirSync(base);
    }

    if (!this.load()) {
      this.save();
    }
  }

  get isInitialized(): boolean {
    return this.initialized;
  }

  public setInitialized(initialized: boolean): void {
    this.initialized = initialized;
    setContext("vscodemm.initialized", initialized);
  }

  public add(server: Server): void {
    this.servers.push(server);
    this.save();
  }

  public save(): void {
    const contents = JSON.stringify({
      initialized: this.initialized,
      servers: this.servers,
    });
    fs.writeFileSync(this.getPath(), contents);
  }

  public getServers(): ServerTreeItem[] {
    let servers = [];
    for (let server of this.servers) {
      servers.push(
        new ServerTreeItem(
          server.name,
          server.address,
          server.port,
          server.secret
        )
      );
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

class ServerList implements vscode.TreeDataProvider<ServerTreeItem> {
  private emitter: vscode.EventEmitter<
    ServerTreeItem | undefined
  > = new vscode.EventEmitter<ServerTreeItem | undefined>();
  readonly onDidChangeTreeData: vscode.Event<ServerTreeItem | undefined> = this
    .emitter.event;

  public constructor() {}

  public getTreeItem(element: ServerTreeItem): vscode.TreeItem {
    return element;
  }

  public getChildren(element?: ServerTreeItem): Thenable<ServerTreeItem[]> {
    if (element === undefined) {
      return Promise.resolve(currentState.getServers());
    }

    return Promise.resolve([]);
  }

  public refresh(): void {
    this.emitter.fire(undefined);
  }
}

class ServerTreeItem extends vscode.TreeItem {
  public constructor(
    public readonly name: string,
    private address: string,
    private port: number,
    private secret: string
  ) {
    super(name, vscode.TreeItemCollapsibleState.None);
  }

  get description(): string {
    return this.address;
  }
}

class HeapVisualizer implements vscode.TreeDataProvider<HeapObject> {
  private buffer: string = "";
  private objects: HeapObject[];

  private emitter: vscode.EventEmitter<
    HeapObject | undefined
  > = new vscode.EventEmitter<HeapObject | undefined>();
  readonly onDidChangeTreeData: vscode.Event<HeapObject | undefined> = this
    .emitter.event;

  public constructor() {}

  public getTreeItem(element: HeapObject): vscode.TreeItem {
    return element;
  }

  public getChildren(element?: HeapObject): Thenable<HeapObject[]> {
    if (element === undefined) {
      return Promise.resolve(this.objects);
    }

    return Promise.resolve([]);
  }

  public clear(): void {
    this.objects = [];
    this.buffer = "";
    this.emitter.fire(undefined);
  }

  public receive(data: string): void {
    while (true) {
      let position = data.indexOf("\n");
      if (position === -1) {
        break;
      }

      let command = JSON.parse(this.buffer + data.substring(0, position));
      data = data.substring(position + 1);
      this.buffer = "";

      let id = command.id;
      let locality = command.at;

      console.log(command.op);
      switch (command.op) {
      }
    }

    this.buffer += data;
    this.emitter.fire(undefined);
  }
}

class HeapObject extends vscode.TreeItem {
  public constructor() {
    super("Heap object", vscode.TreeItemCollapsibleState.Collapsed);
  }
}

async function askNewServer(callback: (Server) => void) {
  const name = await vscode.window.showInputBox({ prompt: "Name: " });
  if (!name) {
    return;
  }

  let address = "";
  while (net.isIP(address) === 0) {
    address = await vscode.window.showInputBox({ prompt: "Address: " });
    if (!address) {
      return;
    }
  }

  let port: number | undefined = undefined;
  do {
    const portString = await vscode.window.showInputBox({ prompt: "Port: " });
    if (!portString) {
      return;
    }

    port = parseInt(portString);
  } while (port === undefined || isNaN(port) || port < 1 || port > 65535);

  const secret = await vscode.window.showInputBox({ prompt: "Password: " });
  if (!secret) {
    return;
  }

  callback(new Server(name, address, port, secret));
}

function testServer(server: Server): boolean {
  return true;
}

export function activate(cobtext: vscode.ExtensionContext) {
  const serverList = new ServerList();
  vscode.window.registerTreeDataProvider("vscodemm.servers", serverList);

  const heapVisualizer = new HeapVisualizer();
  vscode.window.registerTreeDataProvider(
    "vscodemm.heapVisualizer",
    heapVisualizer
  );

  vscode.commands.registerCommand("vscodemm.setup", () => {
    if (!currentState.isInitialized) {
      fs.copySync(
        "vscodemm/",
        path.join(vscode.workspace.rootPath, "vscodemm/")
      );

      currentState.setInitialized(true);
      currentState.save();
    }
  });

  vscode.commands.registerCommand("vscodemm.testServer", () =>
    askNewServer((server) => {
      if (testServer(server)) {
        currentState.add(server);
        serverList.refresh();

        vscode.window.showInformationMessage(
          "Successful connection. Server added to list."
        );
      } else {
        vscode.window.showErrorMessage("Connection failed");
      }
    })
  );

  const debugSocket = net.createServer((debugConnection) => {
    debugConnection.on("end", () => {
      setRunning(false);
      heapVisualizer.clear();
    });

    debugConnection.on("data", (data) =>
      heapVisualizer.receive(data.toString("utf-8"))
    );

    debugConnection.write("{}\n");
    setRunning(true);
  });

  debugSocket.listen(39999, "127.0.0.1");
  setRunning(false);
}
