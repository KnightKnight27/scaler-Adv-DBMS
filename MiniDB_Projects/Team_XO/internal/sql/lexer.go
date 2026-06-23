package sql

import (
	"fmt"
	"strings"
)

type tokenKind int

const (
	tEOF tokenKind = iota
	tIdent
	tNumber
	tString
	tPunct // ( ) , . ;
	tOp    // = != <> < <= > >=
	tStar  // *
	tKeyword
)

type token struct {
	kind tokenKind
	text string
	pos  int
}

// keywords recognised by the lexer. They are matched case-insensitively and
// normalised to upper case so the parser can compare against canonical forms.
var keywords = map[string]bool{
	"CREATE": true, "TABLE": true, "INDEX": true, "ON": true, "PRIMARY": true, "KEY": true,
	"INSERT": true, "INTO": true, "VALUES": true,
	"SELECT": true, "FROM": true, "WHERE": true, "JOIN": true, "INNER": true, "AND": true,
	"DELETE": true, "EXPLAIN": true, "COUNT": true, "AS": true,
	"BEGIN": true, "COMMIT": true, "ROLLBACK": true, "ABORT": true,
	"INT": true, "INTEGER": true, "TEXT": true, "BOOL": true, "BOOLEAN": true,
	"TRUE": true, "FALSE": true,
}

// lexer scans SQL source into a token slice. Errors are reported lazily through
// the token stream by returning an error from tokenize.
type lexer struct {
	src string
	pos int
}

func tokenize(src string) ([]token, error) {
	l := &lexer{src: src}
	var toks []token
	for {
		tok, err := l.next()
		if err != nil {
			return nil, err
		}
		toks = append(toks, tok)
		if tok.kind == tEOF {
			return toks, nil
		}
	}
}

func (l *lexer) next() (token, error) {
	l.skipSpaceAndComments()
	if l.pos >= len(l.src) {
		return token{kind: tEOF, pos: l.pos}, nil
	}
	start := l.pos
	c := l.src[l.pos]

	switch {
	case c == '\'':
		return l.lexString()
	case isDigit(c) || (c == '-' && l.pos+1 < len(l.src) && isDigit(l.src[l.pos+1])):
		return l.lexNumber()
	case isIdentStart(c):
		return l.lexIdent()
	}

	switch c {
	case '(', ')', ',', '.', ';':
		l.pos++
		return token{kind: tPunct, text: string(c), pos: start}, nil
	case '*':
		l.pos++
		return token{kind: tStar, text: "*", pos: start}, nil
	case '=':
		l.pos++
		return token{kind: tOp, text: "=", pos: start}, nil
	case '!':
		if l.peekAt(1) == '=' {
			l.pos += 2
			return token{kind: tOp, text: "!=", pos: start}, nil
		}
	case '<':
		if l.peekAt(1) == '=' {
			l.pos += 2
			return token{kind: tOp, text: "<=", pos: start}, nil
		}
		if l.peekAt(1) == '>' {
			l.pos += 2
			return token{kind: tOp, text: "!=", pos: start}, nil
		}
		l.pos++
		return token{kind: tOp, text: "<", pos: start}, nil
	case '>':
		if l.peekAt(1) == '=' {
			l.pos += 2
			return token{kind: tOp, text: ">=", pos: start}, nil
		}
		l.pos++
		return token{kind: tOp, text: ">", pos: start}, nil
	}
	return token{}, fmt.Errorf("sql: unexpected character %q at position %d", c, l.pos)
}

func (l *lexer) lexString() (token, error) {
	start := l.pos
	l.pos++ // opening quote
	var sb strings.Builder
	for l.pos < len(l.src) {
		c := l.src[l.pos]
		if c == '\'' {
			// Doubled quote is an escaped single quote.
			if l.peekAt(1) == '\'' {
				sb.WriteByte('\'')
				l.pos += 2
				continue
			}
			l.pos++
			return token{kind: tString, text: sb.String(), pos: start}, nil
		}
		sb.WriteByte(c)
		l.pos++
	}
	return token{}, fmt.Errorf("sql: unterminated string literal starting at %d", start)
}

func (l *lexer) lexNumber() (token, error) {
	start := l.pos
	if l.src[l.pos] == '-' {
		l.pos++
	}
	for l.pos < len(l.src) && isDigit(l.src[l.pos]) {
		l.pos++
	}
	return token{kind: tNumber, text: l.src[start:l.pos], pos: start}, nil
}

func (l *lexer) lexIdent() (token, error) {
	start := l.pos
	for l.pos < len(l.src) && isIdentPart(l.src[l.pos]) {
		l.pos++
	}
	text := l.src[start:l.pos]
	upper := strings.ToUpper(text)
	if keywords[upper] {
		return token{kind: tKeyword, text: upper, pos: start}, nil
	}
	return token{kind: tIdent, text: text, pos: start}, nil
}

func (l *lexer) skipSpaceAndComments() {
	for l.pos < len(l.src) {
		c := l.src[l.pos]
		if c == ' ' || c == '\t' || c == '\n' || c == '\r' {
			l.pos++
			continue
		}
		// SQL line comments: -- to end of line.
		if c == '-' && l.peekAt(1) == '-' {
			for l.pos < len(l.src) && l.src[l.pos] != '\n' {
				l.pos++
			}
			continue
		}
		return
	}
}

func (l *lexer) peekAt(offset int) byte {
	if l.pos+offset < len(l.src) {
		return l.src[l.pos+offset]
	}
	return 0
}

func isDigit(c byte) bool      { return c >= '0' && c <= '9' }
func isIdentStart(c byte) bool { return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') }
func isIdentPart(c byte) bool  { return isIdentStart(c) || isDigit(c) }
