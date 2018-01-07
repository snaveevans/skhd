#include "parse.h"
#include "tokenize.h"
#include "locale.h"
#include "hotkey.h"
#include "hashtable.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#define internal static

internal struct mode *
init_default_mode(struct parser *parser)
{
    struct mode *default_mode = malloc(sizeof(struct mode));

    default_mode->line = -1;
    default_mode->cursor = -1;
    default_mode->name = copy_string("default");

    table_init(&default_mode->hotkey_map, 131,
               (table_hash_func) hash_hotkey,
               (table_compare_func) same_hotkey);

    default_mode->command = NULL;
    table_add(parser->mode_map, default_mode->name, default_mode);

    return default_mode;
}

internal char *
read_file(const char *file)
{
    unsigned length;
    char *buffer = NULL;
    FILE *handle = fopen(file, "r");

    if (handle) {
        fseek(handle, 0, SEEK_END);
        length = ftell(handle);
        fseek(handle, 0, SEEK_SET);
        buffer = malloc(length + 1);
        fread(buffer, length, 1, handle);
        buffer[length] = '\0';
        fclose(handle);
    }

    return buffer;
}

internal char *
copy_string_count(char *s, int length)
{
    char *result = malloc(length + 1);
    memcpy(result, s, length);
    result[length] = '\0';
    return result;
}

internal uint32_t
keycode_from_hex(char *hex)
{
    uint32_t result;
    sscanf(hex, "%x", &result);
    return result;
}

internal char *
parse_command(struct parser *parser)
{
    struct token command = parser_previous(parser);
    char *result = copy_string_count(command.text, command.length);
    printf("\tcmd: '%s'\n", result);
    return result;
}

internal uint32_t
parse_key_hex(struct parser *parser)
{
    struct token key = parser_previous(parser);
    char *hex = copy_string_count(key.text, key.length);
    uint32_t keycode = keycode_from_hex(hex);
    free(hex);
    printf("\tkey: '%.*s' (0x%02x)\n", key.length, key.text, keycode);
    return keycode;
}

internal uint32_t
parse_key(struct parser *parser)
{
    uint32_t keycode;
    struct token key = parser_previous(parser);
    keycode = keycode_from_char(*key.text);
    printf("\tkey: '%c' (0x%02x)\n", *key.text, keycode);
    return keycode;
}

#define KEY_HAS_IMPLICIT_FN_MOD 4
internal uint32_t literal_keycode_value[] =
{
    kVK_Return,     kVK_Tab,           kVK_Space,
    kVK_Delete,     kVK_Escape,        kVK_ForwardDelete,
    kVK_Home,       kVK_End,           kVK_PageUp,
    kVK_PageDown,   kVK_Help,          kVK_LeftArrow,
    kVK_RightArrow, kVK_UpArrow,       kVK_DownArrow,
    kVK_F1,         kVK_F2,            kVK_F3,
    kVK_F4,         kVK_F5,            kVK_F6,
    kVK_F7,         kVK_F8,            kVK_F9,
    kVK_F10,        kVK_F11,           kVK_F12,
    kVK_F13,        kVK_F14,           kVK_F15,
    kVK_F16,        kVK_F17,           kVK_F18,
    kVK_F19,        kVK_F20,
};

internal void
parse_key_literal(struct parser *parser, struct hotkey *hotkey)
{
    struct token key = parser_previous(parser);
    for (int i = 0; i < array_count(literal_keycode_str); ++i) {
        if (token_equals(key, literal_keycode_str[i])) {
            if (i > KEY_HAS_IMPLICIT_FN_MOD) hotkey->flags |= Hotkey_Flag_Fn;
            hotkey->key = literal_keycode_value[i];
            printf("\tkey: '%.*s' (0x%02x)\n", key.length, key.text, hotkey->key);
            break;
        }
    }
}

internal enum hotkey_flag modifier_flags_value[] =
{
    Hotkey_Flag_Alt,        Hotkey_Flag_LAlt,       Hotkey_Flag_RAlt,
    Hotkey_Flag_Shift,      Hotkey_Flag_LShift,     Hotkey_Flag_RShift,
    Hotkey_Flag_Cmd,        Hotkey_Flag_LCmd,       Hotkey_Flag_RCmd,
    Hotkey_Flag_Control,    Hotkey_Flag_LControl,   Hotkey_Flag_RControl,
    Hotkey_Flag_Fn,         Hotkey_Flag_Hyper,
};

internal uint32_t
parse_modifier(struct parser *parser)
{
    struct token modifier = parser_previous(parser);
    uint32_t flags = 0;

    for (int i = 0; i < array_count(modifier_flags_str); ++i) {
        if (token_equals(modifier, modifier_flags_str[i])) {
            flags |= modifier_flags_value[i];
            printf("\tmod: '%s'\n", modifier_flags_str[i]);
            break;
        }
    }

    if (parser_match(parser, Token_Plus)) {
        if (parser_match(parser, Token_Modifier)) {
            flags |= parse_modifier(parser);
        } else {
            parser_report_error(parser, Error_Unexpected_Token, "expected modifier");
        }
    }

    return flags;
}

internal void
parse_mode(struct parser *parser, struct hotkey *hotkey)
{
    struct token identifier = parser_previous(parser);

    char *name = copy_string_count(identifier.text, identifier.length);
    struct mode *mode = table_find(parser->mode_map, name);
    free(name);

    if (!mode && token_equals(identifier, "default")) {
        mode = init_default_mode(parser);
    }

    if (!mode) {
        parser_report_error(parser, Error_Undeclared_Ident, "undeclared identifier");
        return;
    }

    hotkey->mode_list[hotkey->mode_count++] = mode;
    printf("\tmode: '%s'\n", mode->name);

    if (parser_match(parser, Token_Comma)) {
        if (parser_match(parser, Token_Identifier)) {
            parse_mode(parser, hotkey);
        } else {
            parser_report_error(parser, Error_Unexpected_Token, "expected identifier");
            return;
        }
    }
}

internal struct hotkey *
parse_hotkey(struct parser *parser)
{
    struct hotkey *hotkey = malloc(sizeof(struct hotkey));
    memset(hotkey, 0, sizeof(struct hotkey));
    bool found_modifier;

    printf("hotkey :: #%d {\n", parser->current_token.line);

    if (parser_match(parser, Token_Identifier)) {
        parse_mode(parser, hotkey);
        if (parser->error) {
            goto err;
        }
    }

    if (hotkey->mode_count > 0) {
        if (!parser_match(parser, Token_Insert)) {
            parser_report_error(parser, Error_Unexpected_Token, "expected '<'");
            goto err;
        }
    } else {
        hotkey->mode_list[hotkey->mode_count] = table_find(parser->mode_map, "default");
        if (!hotkey->mode_list[hotkey->mode_count]) {
            hotkey->mode_list[hotkey->mode_count] = init_default_mode(parser);
        }
        hotkey->mode_count++;
    }

    if ((found_modifier = parser_match(parser, Token_Modifier))) {
        hotkey->flags = parse_modifier(parser);
        if (parser->error) {
            goto err;
        }
    }

    if (found_modifier) {
        if (!parser_match(parser, Token_Dash)) {
            parser_report_error(parser, Error_Unexpected_Token, "expected '-'");
            goto err;
        }
    }

    if (parser_match(parser, Token_Key)) {
        hotkey->key = parse_key(parser);
    } else if (parser_match(parser, Token_Key_Hex)) {
        hotkey->key = parse_key_hex(parser);
    } else if (parser_match(parser, Token_Literal)) {
        parse_key_literal(parser, hotkey);
    } else {
        parser_report_error(parser, Error_Unexpected_Token, "expected key-literal");
        goto err;
    }

    if (parser_match(parser, Token_Arrow)) {
        hotkey->flags |= Hotkey_Flag_Passthrough;
    }

    if (parser_match(parser, Token_Command)) {
        hotkey->command = parse_command(parser);
    } else if (parser_match(parser, Token_Activate)) {
        hotkey->flags |= Hotkey_Flag_Activate;
        hotkey->command = parse_command(parser);
        if (!table_find(parser->mode_map, hotkey->command)) {
            parser_report_error(parser, Error_Undeclared_Ident, "undeclared identifier");
            goto err;
        }
    } else {
        parser_report_error(parser, Error_Unexpected_Token, "expected ':' followed by command or ';' followed by mode");
        goto err;
    }

    printf("}\n");

    return hotkey;

err:
    free(hotkey);
    return NULL;
}

internal struct mode *
parse_mode_decl(struct parser *parser)
{
    struct mode *mode = malloc(sizeof(struct mode));
    struct token identifier = parser_previous(parser);

    mode->line = identifier.line;
    mode->cursor = identifier.cursor;
    mode->name = copy_string_count(identifier.text, identifier.length);

    table_init(&mode->hotkey_map, 131,
               (table_hash_func) hash_hotkey,
               (table_compare_func) same_hotkey);

    if (parser_match(parser, Token_Command)) {
        mode->command = copy_string_count(parser->previous_token.text, parser->previous_token.length);
    } else {
        mode->command = NULL;
    }

    return mode;
}

void parse_declaration(struct parser *parser)
{
    struct mode *mode;
    parser_match(parser, Token_Decl);
    if (parser_match(parser, Token_Identifier)) {
        mode = parse_mode_decl(parser);
        if (table_find(parser->mode_map, mode->name)) {
            parser_report_error(parser, Error_Duplicate_Ident,
                                "#%d:%d duplicate declaration '%s'\n",
                                mode->line, mode->cursor, mode->name);
        } else {
            table_add(parser->mode_map, mode->name, mode);
        }
    } else {
        parser_report_error(parser, Error_Unexpected_Token, "expected identifier");
    }
}

void parse_config(struct parser *parser)
{
    struct mode *mode;
    struct hotkey *hotkey;

    while (!parser_eof(parser)) {
        if (parser->error) {
            free_mode_map(parser->mode_map);
            return;
        }

        if ((parser_check(parser, Token_Identifier)) ||
            (parser_check(parser, Token_Modifier)) ||
            (parser_check(parser, Token_Literal)) ||
            (parser_check(parser, Token_Key_Hex)) ||
            (parser_check(parser, Token_Key))) {
            if ((hotkey = parse_hotkey(parser))) {
                for (int i = 0; i < hotkey->mode_count; ++i) {
                    mode = hotkey->mode_list[i];
                    table_add(&mode->hotkey_map, hotkey, hotkey);
                }
            }
        } else if (parser_check(parser, Token_Decl)) {
            parse_declaration(parser);
        } else {
            parser_report_error(parser, Error_Unexpected_Token, "expected decl, modifier or key-literal");
        }
    }
}

struct token
parser_peek(struct parser *parser)
{
    return parser->current_token;
}

struct token
parser_previous(struct parser *parser)
{
    return parser->previous_token;
}

bool parser_eof(struct parser *parser)
{
    struct token token = parser_peek(parser);
    return token.type == Token_EndOfStream;
}

struct token
parser_advance(struct parser *parser)
{
    if (!parser_eof(parser)) {
        parser->previous_token = parser->current_token;
        parser->current_token = get_token(&parser->tokenizer);
    }
    return parser_previous(parser);
}

bool parser_check(struct parser *parser, enum token_type type)
{
    if (parser_eof(parser)) return false;
    struct token token = parser_peek(parser);
    return token.type == type;
}

bool parser_match(struct parser *parser, enum token_type type)
{
    if (parser_check(parser, type)) {
        parser_advance(parser);
        return true;
    }
    return false;
}

void parser_report_error(struct parser *parser, enum parse_error_type error_type, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    if (error_type == Error_Unexpected_Token) {
        fprintf(stderr, "#%d:%d ", parser->current_token.line, parser->current_token.cursor);
        vfprintf(stderr, format, args);
        fprintf(stderr, ", but got '%.*s'\n", parser->current_token.length, parser->current_token.text);
    } else if (error_type == Error_Undeclared_Ident) {
        fprintf(stderr, "#%d:%d ", parser->previous_token.line, parser->previous_token.cursor);
        vfprintf(stderr, format, args);
        fprintf(stderr, " '%.*s'\n", parser->previous_token.length, parser->previous_token.text);
    } else if (error_type == Error_Duplicate_Ident) {
        vfprintf(stderr, format, args);
    }

    va_end(args);
    parser->error = true;
}

bool parser_init(struct parser *parser, char *file)
{
    memset(parser, 0, sizeof(struct parser));
    char *buffer = read_file(file);
    if (buffer) {
        tokenizer_init(&parser->tokenizer, buffer);
        parser_advance(parser);
        return true;
    }
    return false;
}

void parser_destroy(struct parser *parser)
{
    free(parser->tokenizer.buffer);
}
