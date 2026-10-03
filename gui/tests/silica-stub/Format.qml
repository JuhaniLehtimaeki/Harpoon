pragma Singleton
import QtQuick 2.0
QtObject {
    function formatDate(date, format) {
        if (!(date instanceof Date) || isNaN(date)) { console.error("formatDate called with a non-date: " + date) }
        return Qt.formatDateTime(date)
    }
    function formatFileSize(bytes) {
        if (typeof bytes !== "number" || isNaN(bytes)) { console.error("formatFileSize called with a non-number: " + bytes) }
        return bytes + " B"
    }
}
