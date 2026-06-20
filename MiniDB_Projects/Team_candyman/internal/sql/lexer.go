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
	tPunct   // ( ) , . ; *
	tOp      // = != <> < <= > >=
	tKeyword // reserved words (upper-cased)
)

type token struct {
	kind tokenKind
	text string
	pos  int
}

var keywords = map[string]bool{
	"CREATE": true, "TABLE": true, "PRIMARY": true, "KEY": true,
	"INSERT": true, "INTO": true, "VALUES": true,
	"DELETE": true, "FROM": true, "WHERE": true,
	"SELECT": true, "JOIN": true, "ON": true, "AND": true, "AS": true,
	"GROUP": true, "BY": true, "EXPLAIN": true,
	"BEGIN": true, "COMMIT": true, "ROLLBACK": true,
	"INT": true, "INTEGER": true, "BIGINT": true,
	"TEXT": true, "STRING": true, "VARCHAR": true,
	"NULL":  true,
	"COUNT": true, "SUM": true, "AVG": true, "MIN": true, "MAX": true,
}

type lexer struct {
	src string
	pos int
}

func newLexer(src string) *lexer { return &lexer{src: src} }

func (l *lexer) errf(format string, a ...any) error {
	return fmt.Errorf("sql: "+format+" (at offset %d)", append(a, l.pos)...)
}

func (l *lexer) tokens() ([]token, error) {
	var toks []token
	for {
		t, err := l.next()
		if err != nil {
			return nil, err
		}
		toks = append(toks, t)
		if t.kind == tEOF {
			return toks, nil
		}
	}
}

func (l *lexer) next() (token, error) {
	for l.pos < len(l.src) {
		c := l.src[l.pos]
		switch {
		case c == ' ' || c == '\t' || c == '\n' || c == '\r':
			l.pos++
			continue
		case c == '-' && l.pos+1 < len(l.src) && l.src[l.pos+1] == '-':
			// line comment
			for l.pos < len(l.src) && l.src[l.pos] != '\n' {
				l.pos++
			}
			continue
		}
		break
	}
	if l.pos >= len(l.src) {
		return token{kind: tEOF, pos: l.pos}, nil
	}
	start := l.pos
	c := l.src[l.pos]
	switch {
	case isIdentStart(c):
		for l.pos < len(l.src) && isIdentPart(l.src[l.pos]) {
			l.pos++
		}
		word := l.src[start:l.pos]
		up := strings.ToUpper(word)
		if keywords[up] {
			return token{kind: tKeyword, text: up, pos: start}, nil
		}
		return token{kind: tIdent, text: word, pos: start}, nil
	case c >= '0' && c <= '9', c == '-' && l.pos+1 < len(l.src) && l.src[l.pos+1] >= '0' && l.src[l.pos+1] <= '9':
		if c == '-' {
			l.pos++
		}
		for l.pos < len(l.src) && l.src[l.pos] >= '0' && l.src[l.pos] <= '9' {
			l.pos++
		}
		return token{kind: tNumber, text: l.src[start:l.pos], pos: start}, nil
	case c == '\'':
		l.pos++ // opening quote
		var sb strings.Builder
		for l.pos < len(l.src) {
			ch := l.src[l.pos]
			if ch == '\'' {
				// '' is an escaped quote
				if l.pos+1 < len(l.src) && l.src[l.pos+1] == '\'' {
					sb.WriteByte('\'')
					l.pos += 2
					continue
				}
				l.pos++
				return token{kind: tString, text: sb.String(), pos: start}, nil
			}
			sb.WriteByte(ch)
			l.pos++
		}
		return token{}, l.errf("unterminated string literal")
	case strings.IndexByte("(),.;*", c) >= 0:
		l.pos++
		return token{kind: tPunct, text: string(c), pos: start}, nil
	case c == '=' || c == '<' || c == '>' || c == '!':
		l.pos++
		if l.pos < len(l.src) {
			two := l.src[start : l.pos+1]
			switch two {
			case "<=", ">=", "<>", "!=":
				l.pos++
				return token{kind: tOp, text: two, pos: start}, nil
			}
		}
		op := l.src[start:l.pos]
		if op == "!" {
			return token{}, l.errf("unexpected '!'")
		}
		return token{kind: tOp, text: op, pos: start}, nil
	default:
		return token{}, l.errf("unexpected character %q", string(c))
	}
}

func isIdentStart(c byte) bool {
	return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
}
func isIdentPart(c byte) bool { return isIdentStart(c) || (c >= '0' && c <= '9') }
