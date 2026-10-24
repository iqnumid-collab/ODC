import QtQuick
import QtQml

/// Factory for creating and opening ODCPopupDialog instances from a Component.
/// Place the factory immediately before its associated Component. The dialog is created
/// with the factory's parent as the initial parent, then ODCPopupDialog reparents itself
/// to Overlay.overlay on completion.
///
/// Example:
///     ODCPopupDialogFactory {
///         id: myDialogFactory
///         dialogComponent: myDialogComponent
///     }
///
///     Component {
///         id: myDialogComponent
///         ODCPopupDialog { ... }
///     }
///
///     onFoo: myDialogFactory.open()
///     onBar: myDialogFactory.open({ title: "My Title", myProp: someValue })
Item {
    id: root
    visible: false

    required property Component dialogComponent

    function open(props) {
        var dialogParent = root.parent ? root.parent : null
        var dialog = props === undefined || props === null
            ? dialogComponent.createObject(dialogParent)
            : dialogComponent.createObject(dialogParent, props)

        if (!dialog) {
            console.warn("ODCPopupDialogFactory: Failed to create dialog from dialogComponent");
            return null;
        }
        dialog.open()
        return dialog
    }
}
