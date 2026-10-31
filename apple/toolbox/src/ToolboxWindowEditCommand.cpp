/** Included by the native window and its host neighbor. */
bool ToolboxWindow::handleEditCommand(loka::app::EditVerb verb)
{
  ToolboxScenePlatformController *controller = this->scenePlatformController();
  return controller && ToolboxInputDoor::editCommand(*controller, verb);
}
