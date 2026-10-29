.pragma library
function codePointEnd(value, maximum) {
    var index = 0, count = 0
    while (index < value.length && count < maximum) {
        var first = value.charCodeAt(index++)
        if (first >= 0xd800 && first <= 0xdbff && index < value.length) {
            var second = value.charCodeAt(index)
            if (second >= 0xdc00 && second <= 0xdfff) ++index
        }
        ++count
    }
    return index
}
function codePointLength(value) {
    var index = 0, count = 0
    while (index < value.length) {
        var first = value.charCodeAt(index++)
        if (first >= 0xd800 && first <= 0xdbff && index < value.length) {
            var second = value.charCodeAt(index)
            if (second >= 0xdc00 && second <= 0xdfff) ++index
        }
        ++count
    }
    return count
}
function limitCodePoints(value, maximum) { return value.slice(0, codePointEnd(value, maximum)) }
var disclaimer = ["<b>RuTool Installer</b> — неофициальная программа для установки пользовательского русского перевода Blue Archive. Наша команда не является работниками NEXON.", "Программа позволяет выбрать наш пакет перевода, проверить его подлинность и установить содержащиеся в нём файлы в папку игры. Копия для восстановления сохраняется, если исходные файлы подтверждены. Если подтверждённой копии нет, перед установкой появится предупреждение; вернуть оригинальные файлы можно через проверку Steam.", "RuTool и данный перевод являются <b>неофициальным фанатским проектом</b> и не связаны с NEXON, NEXON Games или другими правообладателями Blue Archive и не одобрены ими.", "Blue Archive, его название, персонажи, изображения и другие связанные материалы принадлежат соответствующим правообладателям.", "Используя программу, убедитесь, что Blue Archive закрыта перед установкой перевода. Приятной игры!"];
var reportTopics = ["Общий вопрос", "Ошибки в переводе", "Ошибки в интерфейсе программы", "Техническая проблема", "Предложение об улучшении"];
