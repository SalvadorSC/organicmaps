package app.organicmaps.settings;

import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.text.InputType;
import android.text.TextUtils;
import android.view.View;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.preference.CheckBoxPreference;
import androidx.preference.EditTextPreference;
import androidx.preference.PreferenceCategory;
import androidx.preference.PreferenceScreen;
import androidx.preference.TwoStatePreference;
import app.organicmaps.R;
import app.organicmaps.sdk.bike_share.BikeShare;
import app.organicmaps.sdk.bus_live.BusLive;
import app.organicmaps.sdk.commuter_live.CommuterLive;
import app.organicmaps.sdk.metro_live.MetroLive;

public class BcnTransportSettingsFragment extends BaseXmlSettingsFragment
{
  @Override
  protected int getXmlResources()
  {
    return R.xml.prefs_bcn_transport;
  }

  @Override
  public void onCreatePreferences(@Nullable Bundle bundle, @Nullable String root)
  {
    super.onCreatePreferences(bundle, root);
    PreferenceScreen screen = getPreferenceScreen();
    TwoStatePreference metro = findPreference(getString(R.string.pref_metro_live));
    if (screen == null || metro == null)
      return;
    final int metroOrder = metro.getOrder();
    for (int i = 0; i < screen.getPreferenceCount(); ++i)
    {
      if (screen.getPreference(i).getOrder() > metroOrder)
        screen.getPreference(i).setOrder(screen.getPreference(i).getOrder() + 1);
    }
    PreferenceCategory lines = new PreferenceCategory(requireContext());
    lines.setKey(getString(R.string.pref_metro_lines));
    lines.setTitle(R.string.metro_lines_title);
    lines.setSummary(R.string.metro_lines_summary);
    lines.setOrder(metroOrder + 1);
    screen.addPreference(lines);

    float density = getResources().getDisplayMetrics().density;
    int size = (int) (18 * density);
    for (int i = 0; i < MetroLineSelection.NAMES.length; ++i)
      lines.addPreference(lineBox(MetroLineSelection.NAMES[i], MetroLineSelection.key(MetroLineSelection.NAMES[i]),
                                  MetroLineSelection.COLORS[i], density, size, true));

    TwoStatePreference commuter = findPreference(getString(R.string.pref_commuter_live));
    if (commuter == null)
      return;
    final int commuterOrder = commuter.getOrder();
    for (int i = 0; i < screen.getPreferenceCount(); ++i)
    {
      if (screen.getPreference(i).getOrder() > commuterOrder)
        screen.getPreference(i).setOrder(screen.getPreference(i).getOrder() + 2);
    }
    PreferenceCategory fgc = new PreferenceCategory(requireContext());
    fgc.setKey(getString(R.string.pref_fgc_lines));
    fgc.setTitle(R.string.fgc_lines_title);
    fgc.setSummary(R.string.commuter_lines_summary);
    fgc.setOrder(commuterOrder + 1);
    screen.addPreference(fgc);
    PreferenceCategory rodalies = new PreferenceCategory(requireContext());
    rodalies.setKey(getString(R.string.pref_rodalies_lines));
    rodalies.setTitle(R.string.rodalies_lines_title);
    rodalies.setSummary(R.string.commuter_lines_summary);
    rodalies.setOrder(commuterOrder + 2);
    screen.addPreference(rodalies);
    for (int i = 0; i < CommuterLineSelection.FGC_NAMES.length; ++i)
      fgc.addPreference(
          commuterLine(CommuterLineSelection.FGC_NAMES[i],
                       CommuterLineSelection.key(CommuterLineSelection.FGC, CommuterLineSelection.FGC_NAMES[i]),
                       CommuterLineSelection.FGC_COLORS[i], density, size));
    for (int i = 0; i < CommuterLineSelection.RODALIES_NAMES.length; ++i)
      rodalies.addPreference(commuterLine(
          CommuterLineSelection.RODALIES_NAMES[i],
          CommuterLineSelection.key(CommuterLineSelection.RODALIES, CommuterLineSelection.RODALIES_NAMES[i]),
          CommuterLineSelection.RODALIES_COLORS[i], density, size));
  }

  @NonNull
  private CheckBoxPreference commuterLine(@NonNull String title, @NonNull String key, int color, float density,
                                          int size)
  {
    CheckBoxPreference box = lineBox(title, key, color, density, size, false);
    box.setDependency(getString(R.string.pref_commuter_live));
    return box;
  }

  @NonNull
  private CheckBoxPreference lineBox(@NonNull String title, @NonNull String key, int color, float density, int size,
                                     boolean defaultOn)
  {
    CheckBoxPreference box = new CheckBoxPreference(requireContext());
    box.setKey(key);
    box.setTitle(title);
    box.setDefaultValue(defaultOn);
    box.setPersistent(true);
    GradientDrawable badge = new GradientDrawable();
    badge.setShape(GradientDrawable.OVAL);
    badge.setColor(color);
    badge.setStroke(Math.max(1, (int) density), 0x66000000);
    badge.setSize(size, size);
    box.setIcon(badge);
    return box;
  }

  @Override
  public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState)
  {
    super.onViewCreated(view, savedInstanceState);

    final TwoStatePreference bikeShare = getPreference(getString(R.string.pref_bike_share));
    bikeShare.setChecked(BikeShare.isEnabled());
    bikeShare.setOnPreferenceChangeListener((preference, newValue) -> {
      BikeShare.setEnabled((Boolean) newValue);
      return true;
    });

    final TwoStatePreference busArrivals = getPreference(getString(R.string.pref_bus_arrivals));
    busArrivals.setChecked(BusLive.isEnabled());
    busArrivals.setOnPreferenceChangeListener((preference, newValue) -> {
      BusLive.setEnabled((Boolean) newValue);
      return true;
    });

    final TwoStatePreference metroLive = getPreference(getString(R.string.pref_metro_live));
    metroLive.setChecked(MetroLive.isEnabled());
    metroLive.setOnPreferenceChangeListener((preference, newValue) -> {
      MetroLive.setEnabled((Boolean) newValue);
      return true;
    });

    final TwoStatePreference commuterLive = getPreference(getString(R.string.pref_commuter_live));
    commuterLive.setChecked(CommuterLive.isEnabled());
    commuterLive.setOnPreferenceChangeListener((preference, newValue) -> {
      CommuterLive.setEnabled((Boolean) newValue);
      return true;
    });

    final EditTextPreference appId = getPreference(getString(R.string.pref_tmb_app_id));
    appId.setText(BusLive.getTmbAppId());
    appId.setSummaryProvider(preference -> {
      final CharSequence text = ((EditTextPreference) preference).getText();
      if (TextUtils.isEmpty(text))
        return getString(R.string.bcn_transport_tmb_id_empty);
      return text;
    });
    appId.setOnBindEditTextListener(edit -> edit.setInputType(InputType.TYPE_CLASS_TEXT));
    appId.setOnPreferenceChangeListener((preference, newValue) -> {
      final String value = newValue == null ? "" : String.valueOf(newValue);
      BusLive.setTmbCredentials(value, BusLive.getTmbAppKey());
      return true;
    });

    final EditTextPreference appKey = getPreference(getString(R.string.pref_tmb_app_key));
    appKey.setText(BusLive.getTmbAppKey());
    appKey.setSummaryProvider(preference -> {
      final CharSequence text = ((EditTextPreference) preference).getText();
      if (TextUtils.isEmpty(text))
        return getString(R.string.bcn_transport_tmb_key_empty);
      return getString(R.string.bcn_transport_tmb_key_saved);
    });
    appKey.setOnBindEditTextListener(
        edit -> edit.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD));
    appKey.setOnPreferenceChangeListener((preference, newValue) -> {
      final String value = newValue == null ? "" : String.valueOf(newValue);
      BusLive.setTmbCredentials(BusLive.getTmbAppId(), value);
      return true;
    });
  }
}
