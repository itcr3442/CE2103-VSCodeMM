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

class HeapVisualizer implements vscode.TreeDataProvider<HeapTreeItem> {
  private buffer: string = "";
  private objects: HeapObject[] = [];

  private emitter: vscode.EventEmitter<
    HeapTreeItem | undefined
  > = new vscode.EventEmitter<HeapTreeItem | undefined>();

  readonly onDidChangeTreeData: vscode.Event<HeapTreeItem | undefined> = this
    .emitter.event;

  public constructor() {}

  public getTreeItem(element: HeapTreeItem): vscode.TreeItem {
    return element;
  }

  public getChildren(element?: HeapTreeItem): Thenable<HeapTreeItem[]> {
    if (element === undefined) {
      return Promise.resolve(
        this.objects.map(
          (object) =>
            new HeapTreeItem(object.address, object.type, false, [
              new HeapTreeItem("Value", object.value, true, []),
              new HeapTreeItem("Locality", object.at, true, []),
              new HeapTreeItem(
                "References",
                object.references.toString(),
                true,
                []
              ),
            ])
        )
      );
    }

    return Promise.resolve(element.children);
  }

  public clear(): void {
    this.objects = [];
    this.buffer = "";
    this.emitter.fire(undefined);
  }

  public receive(data: string): void {
    while (true) {
      const position = data.indexOf("\n");
      if (position === -1) {
        break;
      }

      const command = JSON.parse(this.buffer + data.substring(0, position));
      data = data.substring(position + 1);
      this.buffer = "";

      const id = command.id;
      const at = command.at;

      if (command.op == "alloc") {
        this.objects.push(
          new HeapObject(id, at, command.type, command.address)
        );
        continue;
      }

      const index = this.objects.findIndex(
        (object) => object.objectID == id && object.at == at
      );
      const object = this.objects[index];

      switch (command.op) {
        case "write":
          object.value = command.value;
          break;

        case "drop":
          if (object.drop() == 0) {
            this.objects.splice(index, 1);
          }
          break;

        case "lift":
          object.lift();
          break;
      }
    }

    this.buffer += data;
    this.emitter.fire(undefined);
  }
}

class HeapTreeItem extends vscode.TreeItem {
  public constructor(
    readonly label: string,
    readonly description: string,
    leaf: boolean,
    readonly children: HeapTreeItem[]
  ) {
    super(
      label,
      leaf
        ? vscode.TreeItemCollapsibleState.None
        : vscode.TreeItemCollapsibleState.Collapsed
    );
  }
}

class HeapObject {
  private _references: number = 1;
  public value: string = "";

  public constructor(
    readonly objectID: number,
    readonly at: string,
    readonly type: string,
    readonly address: string
  ) {}

  get references(): number {
    return this._references;
  }

  public drop(): number {
    return --this._references;
  }

  public lift(): void {
    ++this._references;
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
