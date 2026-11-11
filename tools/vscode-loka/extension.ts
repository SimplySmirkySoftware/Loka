import * as vscode from 'vscode';
import {wire} from './src/wire.ts';
let activation: vscode.Disposable | undefined;
export function activate(context: vscode.ExtensionContext): void {
  activation = wire(vscode, context);
  context.subscriptions.push(activation);
}
export function deactivate(): void { activation?.dispose(); activation = undefined; }
