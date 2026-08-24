function Component()
{
    // default constructor
}

Component.prototype.createOperations = function()
{
    // call default implementation
    component.createOperations();

    if (systemInfo.productType === "windows") {
        // Create shortcut in the Start Menu
        component.addOperation("CreateShortcut", "@TargetDir@/ttv_studio.exe", "@StartMenuDir@/TTV Studio.lnk",
                               "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/ttv_studio.exe");

        // Create shortcut on the Desktop
        component.addOperation("CreateShortcut", "@TargetDir@/ttv_studio.exe", "@DesktopDir@/TTV Studio.lnk",
                               "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/ttv_studio.exe");
    }
}
