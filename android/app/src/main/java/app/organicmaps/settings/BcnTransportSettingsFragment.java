package app.organicmaps.settings;

import android.os.Bundle;
import android.text.InputType;
import android.text.TextUtils;
import android.view.View;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.preference.EditTextPreference;
import androidx.preference.TwoStatePreference;
import app.organicmaps.R;
import app.organicmaps.sdk.bike_share.BikeShare;
import app.organicmaps.sdk.bus_live.BusLive;

public class BcnTransportSettingsFragment extends BaseXmlSettingsFragment
{
  @Override
  protected int getXmlResources()
  {
    return R.xml.prefs_bcn_transport;
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
