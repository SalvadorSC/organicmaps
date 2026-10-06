package app.organicmaps.settings;

import android.content.Context;
import android.content.SharedPreferences;
import androidx.annotation.NonNull;
import androidx.preference.PreferenceManager;

/** Which FGC and Rodalies lines are drawn. Missing keys stay on. */
public final class CommuterLineSelection
{
  public static final String FGC = "fgc";
  public static final String RODALIES = "rodalies";

  public static final String[] FGC_NAMES = {"L6", "L7",  "L8",  "L12", "S1",  "S2",  "S3",  "S4",  "S8", "S9",
                                            "R5", "R50", "R5R", "R6",  "R60", "R6R", "R61", "R62", "M1", "M2"};
  public static final int[] FGC_COLORS = {0xFF797FBC, 0xFFB2600B, 0xFFE274AA, 0xFFB2AED3, 0xFFEF7900,
                                          0xFF88BB0B, 0xFF4F868E, 0xFFA78600, 0xFF49C0DE, 0xFFDF4661,
                                          0xFF3DBFC3, 0xFF00738A, 0xFF3DBFC3, 0xFFB3B3B3, 0xFF5B5B5B,
                                          0xFFB3B3B3, 0xFFB3B3B3, 0xFFB3B3B3, 0xFF000000, 0xFF000000};

  public static final String[] RODALIES_NAMES = {"R1",  "R2",  "R2N", "R2S", "R3",  "R4",  "R7", "R8",
                                                 "R11", "R12", "R13", "R14", "R15", "R16", "R17"};
  public static final int[] RODALIES_COLORS = {0xFF4499D4, 0xFF009900, 0xFF99C83E, 0xFF00642E, 0xFFFF131A,
                                               0xFFFF9221, 0xFFBD7DB5, 0xFF9B1987, 0xFF0064A5, 0xFFFFDC00,
                                               0xFFE52E87, 0xFF675199, 0xFF9A8A76, 0xFFAF0036, 0xFFE97300};

  private CommuterLineSelection() {}

  @NonNull
  public static String key(@NonNull String source, @NonNull String line)
  {
    return source + "_line_" + line;
  }

  public static boolean isShown(@NonNull Context context, @NonNull String source, @NonNull String line)
  {
    return prefs(context).getBoolean(key(source, line), true);
  }

  @NonNull
  public static SharedPreferences prefs(@NonNull Context context)
  {
    return PreferenceManager.getDefaultSharedPreferences(context);
  }
}
