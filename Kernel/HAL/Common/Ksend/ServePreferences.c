/*
 * ServePreferences.c — 热传板端默认（BLOCKS.DB）
 *
 * hotsend.port / hotsend.reboot / hotsend.backup
 * 主机 ksend 行 --no-reboot / --no-backup 可覆盖后两者。
 */
#include "Ksend/Private.h"
#include "Db.h"

#define KSEND_TCP_PORT_DEFAULT 26992u
#define KSEND_PREFERENCE_PORT_KEY    "hotsend.port"
#define KSEND_PREFERENCE_REBOOT_KEY  "hotsend.reboot"
#define KSEND_PREFERENCE_BACKUP_KEY  "hotsend.backup"

static UINT16 ParsePortText(const char *Text) {
    UINT32 Value = 0;

    if (Text == 0 || Text[0] == 0) {
        return 0;
    }
    while (*Text >= '0' && *Text <= '9') {
        Value = Value * 10u + (UINT32)(*Text - '0');
        if (Value > 65535u) {
            return 0;
        }
        Text++;
    }
    if (*Text != 0) {
        return 0;
    }
    return (UINT16)Value;
}

static void FormatPortText(UINT16 Port, char *Out, UINTN Max) {
    char Digits[6];
    int Length = 0;
    int Index;
    UINT32 Value = Port;

    if (Out == 0 || Max == 0u) {
        return;
    }
    if (Value == 0u) {
        Digits[Length++] = '0';
    } else {
        while (Value > 0u && Length < (int)sizeof(Digits)) {
            Digits[Length++] = (char)('0' + (Value % 10u));
            Value /= 10u;
        }
    }
    if ((UINTN)Length + 1u > Max) {
        Out[0] = 0;
        return;
    }
    for (Index = 0; Index < Length; Index++) {
        Out[Index] = Digits[Length - 1 - Index];
    }
    Out[Length] = 0;
}

static int ParseZeroOne(const char *Text, int DefaultValue) {
    if (Text == 0 || Text[0] == 0) {
        return DefaultValue;
    }
    if (Text[0] == '0' && Text[1] == 0) {
        return 0;
    }
    if (Text[0] == '1' && Text[1] == 0) {
        return 1;
    }
    return DefaultValue;
}

UINT16 KsendServeDefaultPort(void) {
    return KsendServePreferencePort();
}

UINT16 KsendServePreferencePort(void) {
    char Value[DB_VAL_MAX];
    UINT16 Port;

    if (DbGet(KSEND_PREFERENCE_PORT_KEY, Value, sizeof(Value)) != DB_OK) {
        return (UINT16)KSEND_TCP_PORT_DEFAULT;
    }
    Port = ParsePortText(Value);
    if (Port == 0u) {
        return (UINT16)KSEND_TCP_PORT_DEFAULT;
    }
    return Port;
}

void KsendServePreferenceSetPort(UINT16 Port) {
    char Value[8];

    if (Port == 0u) {
        Port = (UINT16)KSEND_TCP_PORT_DEFAULT;
    }
    FormatPortText(Port, Value, sizeof(Value));
    (void)DbSet(KSEND_PREFERENCE_PORT_KEY, Value);
}

int KsendServePreferenceReboot(void) {
    char Value[DB_VAL_MAX];

    if (DbGet(KSEND_PREFERENCE_REBOOT_KEY, Value, sizeof(Value)) != DB_OK) {
        return 1;
    }
    return ParseZeroOne(Value, 1);
}

void KsendServePreferenceSetReboot(int On) {
    (void)DbSet(KSEND_PREFERENCE_REBOOT_KEY, On ? "1" : "0");
}

int KsendServePreferenceBackup(void) {
    char Value[DB_VAL_MAX];

    if (DbGet(KSEND_PREFERENCE_BACKUP_KEY, Value, sizeof(Value)) != DB_OK) {
        return 1;
    }
    return ParseZeroOne(Value, 1);
}

void KsendServePreferenceSetBackup(int On) {
    (void)DbSet(KSEND_PREFERENCE_BACKUP_KEY, On ? "1" : "0");
}
