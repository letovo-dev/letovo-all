/**
 * Минимальный Markdown → HTML рендерер для HTML-макетов редактора статей.
 *
 * Задача: повторить СЕМАНТИКУ продового рендера статьи
 * (frontend/src/pages_fsd/articles/ReactMd.tsx), а не заменить его:
 *  - ![alt](file.mp4) превращается в <video>, а не в <img>;
 *  - ссылки на .pdf/.docx/... получают класс downloadLink и атрибут download;
 *  - текст ссылки, содержащий "secret link", получает класс secretLink;
 *  - таблицы оборачиваются в <div class="tableWrapper">;
 *  - href пропускается только для http(s)/mailto/tel.
 *
 * Это макет: парсер намеренно маленький и не покрывает весь CommonMark.
 * В реальной реализации остаются remark-gfm + rehype-raw + rehype-sanitize.
 */
(function (global) {
  'use strict';

  var SAFE_PROTOCOLS = /^(https?|mailto|tel):/i;
  var VIDEO_RE = /\.(mp4|webm|ogg|mkv|avi)(\?.*)?$/i;
  var DOWNLOAD_RE = /\.(pdf|docx?|xlsx?|zip|rar|txt|md)(\?.*)?$/i;
  // Сырой HTML в макете разрешён только для этих тегов (упрощённый аналог rehype-sanitize).
  var RAW_HTML_ALLOWED = /^<\/?(video|source|br|hr|b|strong|i|em|u|s|del|p|div|span|ul|ol|li|h[1-6]|blockquote|table|thead|tbody|tr|th|td|img|a)\b/i;

  function escapeHtml(text) {
    return String(text)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  function safeHref(href) {
    return href && SAFE_PROTOCOLS.test(href) ? href : '#';
  }

  function isVideoUrl(url) {
    return VIDEO_RE.test(url || '');
  }

  function isDownloadableFile(url) {
    return DOWNLOAD_RE.test(url || '');
  }

  /** src/alt здесь уже экранированы: renderInline экранирует текст до разбора ссылок. */
  function mediaTag(src, alt) {
    if (isVideoUrl(src)) {
      var extension = String(src).split('.').pop().toLowerCase().replace(/\?.*$/, '');
      return (
        '<video controls playsinline width="100%" style="max-width:800px;height:auto;z-index:1" src="' +
        src +
        '" aria-label="' +
        (alt || 'Video') +
        '"><source src="' +
        src +
        '" type="video/' +
        extension +
        '">Your browser does not support the video tag.</video>'
      );
    }
    return '<img src="' + src + '" alt="' + (alt || 'Изображение статьи') + '">';
  }

  function linkOpenTag(href, text) {
    var isSecret = text.toLowerCase().indexOf('secret link') !== -1;
    var isDownload = isDownloadableFile(href);
    var className = isSecret ? ' class="secretLink"' : isDownload ? ' class="downloadLink"' : '';
    var download = isDownload ? ' download' : '';
    return '<a href="' + safeHref(href) + '"' + className + download + '>';
  }

  /**
   * Инлайновая разметка внутри одного блока.
   *
   * Готовые теги прячутся в stash под плейсхолдерами @@N@@: иначе последующие
   * замены (например, курсив по `_`) ломают адреса файлов вроде
   * `26_article_icon.webp`.
   */
  function renderInline(text) {
    var stash = [];
    function keep(html) {
      stash.push(html);
      return '@@' + (stash.length - 1) + '@@';
    }

    var out = String(text).replace(/`([^`]+)`/g, function (_m, code) {
      return keep('<code>' + escapeHtml(code) + '</code>');
    });

    out = escapeHtml(out);

    // Картинки и видео: ![alt](src)
    out = out.replace(/!\[([^\]]*)\]\(([^)\s]+)[^)]*\)/g, function (_m, alt, src) {
      return keep(mediaTag(src, alt));
    });

    // Ссылка, обёрнутая вокруг картинки, схлопывается в саму картинку (как в ReactMd.tsx).
    out = out.replace(/\[(@@(\d+)@@)\]\([^)]*\)/g, function (match, placeholder, index) {
      return /^<(img|video)\b/.test(stash[Number(index)]) ? placeholder : match;
    });

    // Ссылки: [текст](href). Текст ссылки остаётся вне stash, чтобы к нему
    // применилось обычное форматирование (**жирный** и т.д.).
    out = out.replace(/\[([^\]]+)\]\(([^)\s]+)[^)]*\)/g, function (_m, label, href) {
      return keep(linkOpenTag(href, label)) + label + keep('</a>');
    });

    out = out
      .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
      .replace(/__([^_]+)__/g, '<strong>$1</strong>')
      .replace(/~~([^~]+)~~/g, '<del>$1</del>')
      .replace(/(^|[^*])\*([^*\n]+)\*/g, '$1<em>$2</em>')
      .replace(/(^|[^_\w])_([^_\n]+)_/g, '$1<em>$2</em>');

    return out.replace(/@@(\d+)@@/g, function (_m, index) {
      return stash[Number(index)];
    });
  }

  function renderTable(rows) {
    var header = rows[0];
    var body = rows.slice(2); // rows[1] — строка выравнивания
    var html = '<div class="tableWrapper" tabindex="0" aria-label="Scrollable table"><table><thead><tr>';
    header.forEach(function (cell) {
      html += '<th>' + renderInline(cell) + '</th>';
    });
    html += '</tr></thead><tbody>';
    body.forEach(function (row) {
      html += '<tr>';
      row.forEach(function (cell) {
        html += '<td>' + renderInline(cell) + '</td>';
      });
      html += '</tr>';
    });
    return html + '</tbody></table></div>';
  }

  function splitRow(line) {
    return line
      .replace(/^\s*\|/, '')
      .replace(/\|\s*$/, '')
      .split('|')
      .map(function (cell) {
        return cell.trim();
      });
  }

  function isTableDelimiter(line) {
    return /^\s*\|?[\s:-]*-[\s:|-]*\|?\s*$/.test(line) && line.indexOf('|') !== -1;
  }

  function renderMarkdown(markdown) {
    var lines = String(markdown == null ? '' : markdown).replace(/\r\n?/g, '\n').split('\n');
    var html = '';
    var i = 0;

    while (i < lines.length) {
      var line = lines[i];

      if (!line.trim()) {
        i++;
        continue;
      }

      // Блок кода ```lang
      if (/^\s*```/.test(line)) {
        var lang = line.replace(/^\s*```/, '').trim();
        var code = [];
        i++;
        while (i < lines.length && !/^\s*```/.test(lines[i])) {
          code.push(lines[i]);
          i++;
        }
        i++;
        html +=
          '<pre><code' +
          (lang ? ' class="language-' + escapeHtml(lang) + '"' : '') +
          '>' +
          escapeHtml(code.join('\n')) +
          '</code></pre>';
        continue;
      }

      // Заголовки
      var heading = /^(#{1,6})\s+(.*)$/.exec(line);
      if (heading) {
        var level = heading[1].length;
        html += '<h' + level + '>' + renderInline(heading[2].trim()) + '</h' + level + '>';
        i++;
        continue;
      }

      // Таблица (строка-разделитель с `|` разбирается здесь, до проверки на <hr>)
      if (line.indexOf('|') !== -1 && i + 1 < lines.length && isTableDelimiter(lines[i + 1])) {
        var rows = [];
        while (i < lines.length && lines[i].indexOf('|') !== -1) {
          rows.push(splitRow(lines[i]));
          i++;
        }
        html += renderTable(rows);
        continue;
      }

      // Горизонтальная линия
      if (/^\s*(-{3,}|\*{3,}|_{3,})\s*$/.test(line)) {
        html += '<hr>';
        i++;
        continue;
      }

      // Цитата
      if (/^\s*>/.test(line)) {
        var quote = [];
        while (i < lines.length && /^\s*>/.test(lines[i])) {
          quote.push(lines[i].replace(/^\s*>\s?/, ''));
          i++;
        }
        html += '<blockquote>' + renderMarkdown(quote.join('\n')) + '</blockquote>';
        continue;
      }

      // Списки (маркированный / нумерованный, один уровень вложенности)
      var bullet = /^(\s*)([-*+]|\d+\.)\s+(.*)$/.exec(line);
      if (bullet) {
        var ordered = /\d/.test(bullet[2]);
        var tag = ordered ? 'ol' : 'ul';
        var items = '';
        while (i < lines.length) {
          var item = /^(\s*)([-*+]|\d+\.)\s+(.*)$/.exec(lines[i]);
          if (!item || /\d/.test(item[2]) !== ordered) break;
          items += '<li>' + renderInline(item[3]) + '</li>';
          i++;
        }
        html += '<' + tag + '>' + items + '</' + tag + '>';
        continue;
      }

      // Сырой HTML (упрощённая версия rehype-raw + sanitize)
      if (/^\s*</.test(line)) {
        var raw = [];
        while (i < lines.length && lines[i].trim()) {
          raw.push(lines[i]);
          i++;
        }
        var rawHtml = raw.join('\n');
        html += RAW_HTML_ALLOWED.test(rawHtml.trim()) ? rawHtml : '<p>' + escapeHtml(rawHtml) + '</p>';
        continue;
      }

      // Абзац
      var paragraph = [];
      while (i < lines.length && lines[i].trim() && !/^\s*(#{1,6}\s|>|```|[-*+]\s|\d+\.\s|<)/.test(lines[i])) {
        paragraph.push(lines[i].trim());
        i++;
      }
      var joined = paragraph.join(' ');
      var mediaOnly = /^!\[[^\]]*\]\([^)]*\)$/.test(joined);
      html += mediaOnly ? renderInline(joined) : '<p>' + renderInline(joined) + '</p>';
    }

    return html;
  }

  var BLOCK_TAGS = 'p,h1,h2,h3,h4,h5,h6,ul,ol,table,pre,blockquote,hr,div,img,video';

  /** HTML → Markdown: то, что уйдёт в .md-файл из WYSIWYG-варианта (макет B). */
  function htmlToMarkdown(root) {
    function children(node, fn) {
      return Array.prototype.map.call(node.childNodes, fn).join('');
    }

    function inline(node) {
      if (node.nodeType === 3) return node.nodeValue.replace(/\s+/g, ' ');
      if (node.nodeType !== 1) return '';
      var tag = node.tagName.toLowerCase();
      var inner = children(node, inline);

      switch (tag) {
        case 'strong':
        case 'b':
          return inner.trim() ? '**' + inner.trim() + '**' : '';
        case 'em':
        case 'i':
          return inner.trim() ? '*' + inner.trim() + '*' : '';
        case 'del':
        case 's':
        case 'strike':
          return inner.trim() ? '~~' + inner.trim() + '~~' : '';
        case 'code':
          return '`' + inner + '`';
        case 'br':
          return '\n';
        case 'img':
          return '![' + (node.getAttribute('alt') || '') + '](' + (node.getAttribute('src') || '') + ')';
        case 'video':
          var source = node.querySelector('source');
          return (
            '![' +
            (node.getAttribute('aria-label') || 'Video') +
            '](' +
            (node.getAttribute('src') || (source && source.getAttribute('src')) || '') +
            ')'
          );
        case 'a':
          return (
            '[' + (inner.trim() || node.getAttribute('href') || '') + '](' + (node.getAttribute('href') || '') + ')'
          );
        default:
          return inner;
      }
    }

    function block(node) {
      if (node.nodeType === 3) {
        var textNode = node.nodeValue.trim();
        return textNode ? textNode + '\n\n' : '';
      }
      if (node.nodeType !== 1) return '';
      var tag = node.tagName.toLowerCase();

      if (/^h[1-6]$/.test(tag)) {
        return new Array(Number(tag[1]) + 1).join('#') + ' ' + children(node, inline).trim() + '\n\n';
      }
      // Обёртки (например, div.tableWrapper) разбираем как блоки, иначе таблица
      // схлопнется в строку текста.
      if (tag === 'div' && node.querySelector(BLOCK_TAGS)) {
        return children(node, block);
      }
      if (tag === 'p' || tag === 'div') {
        var text = children(node, inline).trim();
        return text ? text + '\n\n' : '';
      }
      if (tag === 'ul' || tag === 'ol') {
        var index = 0;
        return (
          Array.prototype.map
            .call(node.children, function (li) {
              index++;
              return (tag === 'ol' ? index + '. ' : '- ') + children(li, inline).trim();
            })
            .join('\n') + '\n\n'
        );
      }
      if (tag === 'blockquote') {
        return (
          children(node, block)
            .trim()
            .split('\n')
            .map(function (l) {
              return '> ' + l;
            })
            .join('\n') + '\n\n'
        );
      }
      if (tag === 'pre') {
        return '```\n' + (node.textContent || '').replace(/\n+$/, '') + '\n```\n\n';
      }
      if (tag === 'hr') return '---\n\n';
      if (tag === 'img' || tag === 'video') return inline(node) + '\n\n';
      if (tag === 'table') {
        var out = '';
        var headCells = node.querySelectorAll('thead th');
        if (headCells.length) {
          out +=
            '| ' +
            Array.prototype.map
              .call(headCells, function (c) {
                return children(c, inline).trim();
              })
              .join(' | ') +
            ' |\n';
          out +=
            '|' +
            Array.prototype.map
              .call(headCells, function () {
                return ' --- ';
              })
              .join('|') +
            '|\n';
        }
        Array.prototype.forEach.call(node.querySelectorAll('tbody tr'), function (tr) {
          out +=
            '| ' +
            Array.prototype.map
              .call(tr.cells, function (c) {
                return children(c, inline).trim();
              })
              .join(' | ') +
            ' |\n';
        });
        return out + '\n';
      }
      return children(node, block);
    }

    return children(root, block).replace(/\n{3,}/g, '\n\n').trim() + '\n';
  }

  global.ArticleMarkdown = {
    render: renderMarkdown,
    toMarkdown: htmlToMarkdown,
    isVideoUrl: isVideoUrl,
    isDownloadableFile: isDownloadableFile,
  };
})(window);
