package app.organicmaps.settings;

import android.content.Context;
import android.content.SharedPreferences;
import androidx.annotation.NonNull;
import androidx.preference.PreferenceManager;

/** Which Barcelona metro lines are drawn. Missing keys stay on. */
public final class MetroLineSelection
{
  public static final String[] NAMES = {"L1", "L2", "L3", "L4", "L5", "L9N", "L9S", "L10N", "L10S", "L11"};
  public static final int[] COLORS = {
      0xFFCE1126, 0xFF93248F, 0xFF1EB53A, 0xFFF7A30E, 0xFF005A97, 0xFFFB712B, 0xFFFB712B, 0xFF00A6D6, 0xFF00A6D6,
      0xFF89B94C};

  private MetroLineSelection() {}

  @NonNull
  public static String key(@NonNull String line)
  {
    return "metro_line_" + line;
  }

  public static boolean isShown(@NonNull Context context, @NonNull String line)
  {
    return prefs(context).getBoolean(key(line), true);
  }

  @NonNull
  public static SharedPreferences prefs(@NonNull Context context)
  {
    return PreferenceManager.getDefaultSharedPreferences(context);
  }
}
