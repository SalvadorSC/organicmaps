package app.organicmaps.widget;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.util.AttributeSet;
import android.view.Choreographer;
import android.view.Gravity;
import android.view.MotionEvent;
import android.widget.LinearLayout;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import app.organicmaps.R;
import app.organicmaps.sdk.Framework;
import app.organicmaps.sdk.commuter_live.CommuterLive;
import app.organicmaps.sdk.commuter_live.CommuterSnapshot;
import app.organicmaps.sdk.commuter_live.CommuterTrain;
import app.organicmaps.sdk.metro_live.MetroLive;
import app.organicmaps.sdk.metro_live.MetroSnapshot;
import app.organicmaps.sdk.metro_live.MetroTrain;
import app.organicmaps.sdk.routing.RoutingController;
import app.organicmaps.sdk.util.NetworkPolicy;
import app.organicmaps.settings.CommuterLineSelection;
import app.organicmaps.settings.MetroLineSelection;
import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Estimated label and the tapped-train caption. Metro dots and commuter chevrons are drawn
 * in the map pass, so they stay put during a pan, fling, zoom, rotate, or tilt.
 */
public class MetroTrainOverlay extends LinearLayout
{
  private static final long REFRESH_MS = 20000;

  @Nullable
  private static WeakReference<MetroTrainOverlay> sInstance;

  private final Handler mHandler = new Handler(Looper.getMainLooper());
  private final ExecutorService mExecutor = Executors.newSingleThreadExecutor();
  private final AtomicBoolean mBusy = new AtomicBoolean();
  private final Map<String, MetroTrain> mTrains = new HashMap<>();
  private final Map<String, CommuterTrain> mCommuter = new HashMap<>();
  private final Paint mLabelBg = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final Paint mLabel = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final RectF mLabelBox = new RectF();

  private TextView mBanner;
  private boolean mRunning;
  private boolean mNeedsKey;
  @Nullable
  private String mSelectedKey;
  @Nullable
  private MetroSnapshot mSnapshot;
  @Nullable
  private CommuterSnapshot mCommuterSnapshot;
  private boolean mCaptionScheduled;

  private final Choreographer.FrameCallback mCaptionFrame = new Choreographer.FrameCallback() {
    @Override
    public void doFrame(long frameTimeNanos)
    {
      mCaptionScheduled = false;
      if (!mRunning || mSelectedKey == null)
        return;
      invalidate();
      scheduleCaption();
    }
  };

  private final Runnable mTick = new Runnable() {
    @Override
    public void run()
    {
      if (!mRunning)
        return;
      requestPoll();
      mHandler.postDelayed(this, REFRESH_MS);
    }
  };

  public MetroTrainOverlay(Context context, @Nullable AttributeSet attrs)
  {
    super(context, attrs);
    setOrientation(VERTICAL);
    setGravity(Gravity.TOP);
    float density = getResources().getDisplayMetrics().density;
    mLabelBg.setColor(0xE0000000);
    mLabel.setColor(Color.WHITE);
    mLabel.setTextSize(13f * density);
    mBanner = new TextView(context);
    mBanner.setTextColor(Color.WHITE);
    mBanner.setBackgroundColor(0xCC000000);
    int pad = (int) (8 * density);
    mBanner.setPadding(pad, pad / 2, pad, pad / 2);
    mBanner.setText(R.string.metro_estimated);
    LayoutParams bannerParams = new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT);
    bannerParams.gravity = Gravity.CENTER_HORIZONTAL;
    bannerParams.topMargin = (int) (48 * density);
    addView(mBanner, bannerParams);
    setWillNotDraw(false);
  }

  /** Called from the map tap path. Empty clears the caption. */
  public static void onTrainTapped(@Nullable String key)
  {
    MetroTrainOverlay overlay = sInstance == null ? null : sInstance.get();
    if (overlay == null)
      return;
    overlay.mHandler.post(() -> overlay.showTrain(key));
  }

  public void onHostResume()
  {
    sInstance = new WeakReference<>(this);
    MetroLive.setTapListener(true);
    mRunning = true;
    refreshVisibility();
    mHandler.removeCallbacks(mTick);
    if (getVisibility() == VISIBLE)
    {
      pushTrains();
      mHandler.post(mTick);
    }
    else
    {
      MetroLive.setTrains(null);
      CommuterLive.setTrains(null);
    }
  }

  public void onHostPause()
  {
    mRunning = false;
    mHandler.removeCallbacks(mTick);
    Choreographer.getInstance().removeFrameCallback(mCaptionFrame);
    mCaptionScheduled = false;
    mBusy.set(false);
  }

  @Override
  protected void onDetachedFromWindow()
  {
    if (sInstance != null && sInstance.get() == this)
      sInstance = null;
    onHostPause();
    MetroLive.setTrains(null);
    CommuterLive.setTrains(null);
    MetroLive.setTapListener(false);
    mExecutor.shutdownNow();
    super.onDetachedFromWindow();
  }

  @Override
  public boolean dispatchTouchEvent(MotionEvent event)
  {
    // The map owns gestures. Train taps are hit-tested in mercator.
    return false;
  }

  private void refreshVisibility()
  {
    boolean navigating = RoutingController.get().isNavigating();
    boolean metro = MetroLive.isEnabled() && !navigating;
    boolean commuter = CommuterLive.isEnabled() && !navigating;
    setVisibility(metro || commuter ? VISIBLE : GONE);
    mBanner.setVisibility(metro ? VISIBLE : GONE);
  }

  private void requestPoll()
  {
    if (!mRunning)
      return;
    refreshVisibility();
    if (getVisibility() != VISIBLE)
    {
      MetroLive.setTrains(null);
      CommuterLive.setTrains(null);
      return;
    }
    if (!NetworkPolicy.getCurrentNetworkUsageStatus())
    {
      clearTrains(false);
      return;
    }
    if (!mBusy.compareAndSet(false, true))
      return;
    boolean metroOn = MetroLive.isEnabled();
    boolean commuterOn = CommuterLive.isEnabled();
    mExecutor.execute(() -> {
      MetroSnapshot metro = null;
      CommuterSnapshot commuter = null;
      try
      {
        if (metroOn)
          metro = MetroLive.poll();
        if (commuterOn)
          commuter = CommuterLive.poll();
      }
      catch (RuntimeException ignored)
      {
        metro = null;
        commuter = null;
      }
      MetroSnapshot metroResult = metro;
      CommuterSnapshot commuterResult = commuter;
      mHandler.post(() -> {
        mBusy.set(false);
        if (!mRunning)
          return;
        apply(metroResult, commuterResult);
      });
    });
  }

  private void clearTrains(boolean needsKey)
  {
    mNeedsKey = needsKey;
    mSnapshot = null;
    mCommuterSnapshot = null;
    mTrains.clear();
    mCommuter.clear();
    mSelectedKey = null;
    if (needsKey)
      mBanner.setText(R.string.metro_needs_key);
    else
      mBanner.setText(R.string.metro_estimated);
    MetroLive.setTrains(null);
    CommuterLive.setTrains(null);
    invalidate();
  }

  private void apply(@Nullable MetroSnapshot metro, @Nullable CommuterSnapshot commuter)
  {
    refreshVisibility();
    if (getVisibility() != VISIBLE)
    {
      MetroLive.setTrains(null);
      CommuterLive.setTrains(null);
      return;
    }
    if (!MetroLive.isEnabled())
    {
      mNeedsKey = false;
      mSnapshot = null;
      mTrains.clear();
      MetroLive.setTrains(null);
    }
    else if (metro != null && !metro.mEnabled)
    {
      mSnapshot = null;
      mTrains.clear();
      MetroLive.setTrains(null);
    }
    else if (metro != null && metro.mNeedsKey)
    {
      mNeedsKey = true;
      mSnapshot = null;
      mTrains.clear();
      mBanner.setText(R.string.metro_needs_key);
      MetroLive.setTrains(null);
    }
    else if (metro != null)
    {
      mNeedsKey = false;
      mSnapshot = metro;
      mBanner.setText(R.string.metro_estimated);
    }

    if (!CommuterLive.isEnabled())
    {
      mCommuterSnapshot = null;
      mCommuter.clear();
      CommuterLive.setTrains(null);
    }
    else if (commuter != null && commuter.mEnabled)
      mCommuterSnapshot = commuter;
    else if (commuter != null)
    {
      mCommuterSnapshot = null;
      mCommuter.clear();
      CommuterLive.setTrains(null);
    }
    pushTrains();
  }

  private void pushTrains()
  {
    mTrains.clear();
    mCommuter.clear();
    List<MetroTrain> shown = new ArrayList<>();
    if (mSnapshot != null && mSnapshot.mTrains != null && MetroLive.isEnabled())
    {
      for (MetroTrain train : mSnapshot.mTrains)
      {
        if (train == null || train.mKey == null)
          continue;
        if (train.mLine != null && !MetroLineSelection.isShown(getContext(), train.mLine))
          continue;
        shown.add(train);
        mTrains.put(train.mKey, train);
      }
    }
    List<CommuterTrain> commuterShown = new ArrayList<>();
    if (mCommuterSnapshot != null && mCommuterSnapshot.mTrains != null && CommuterLive.isEnabled())
    {
      for (CommuterTrain train : mCommuterSnapshot.mTrains)
      {
        if (train == null || train.mKey == null || train.mLine == null)
          continue;
        String source = train.mKey.startsWith("rodalies:") ? CommuterLineSelection.RODALIES : CommuterLineSelection.FGC;
        if (!CommuterLineSelection.isShown(getContext(), source, train.mLine))
          continue;
        commuterShown.add(train);
        mCommuter.put(train.mKey, train);
      }
    }
    if (mSelectedKey != null && !mTrains.containsKey(mSelectedKey) && !mCommuter.containsKey(mSelectedKey))
      mSelectedKey = null;
    MetroLive.setTrains(shown.toArray(new MetroTrain[0]));
    CommuterLive.setTrains(commuterShown.toArray(new CommuterTrain[0]), enabledCommuterLines());
    invalidate();
    scheduleCaption();
  }

  @NonNull
  private String[] enabledCommuterLines()
  {
    if (!CommuterLive.isEnabled())
      return new String[0];
    List<String> lines = new ArrayList<>();
    for (String name : CommuterLineSelection.FGC_NAMES)
      if (CommuterLineSelection.isShown(getContext(), CommuterLineSelection.FGC, name))
        lines.add(name);
    for (String name : CommuterLineSelection.RODALIES_NAMES)
      if (CommuterLineSelection.isShown(getContext(), CommuterLineSelection.RODALIES, name))
        lines.add(name);
    return lines.toArray(new String[0]);
  }

  private void showTrain(@Nullable String key)
  {
    if (!mRunning)
      return;
    if (key == null || key.isEmpty() || (!mTrains.containsKey(key) && !mCommuter.containsKey(key)))
      mSelectedKey = null;
    else if (key.equals(mSelectedKey))
      mSelectedKey = null;
    else
      mSelectedKey = key;
    invalidate();
    scheduleCaption();
  }

  private void scheduleCaption()
  {
    if (mSelectedKey == null || mCaptionScheduled)
      return;
    mCaptionScheduled = true;
    Choreographer.getInstance().postFrameCallback(mCaptionFrame);
  }

  @Override
  protected void dispatchDraw(Canvas canvas)
  {
    super.dispatchDraw(canvas);
    if (mSelectedKey == null)
      return;
    // A missing TMB key hides metro captions. Commuter trains do not need that key.
    if (mNeedsKey && !mCommuter.containsKey(mSelectedKey))
      return;
    MetroTrain train = mTrains.get(mSelectedKey);
    CommuterTrain commuter = train == null ? mCommuter.get(mSelectedKey) : null;
    if (train == null && commuter == null)
      return;
    double lat = train != null ? train.mLat : commuter.mLat;
    double lon = train != null ? train.mLon : commuter.mLon;
    double[] px = Framework.nativeLatLonToScreen(lat, lon);
    if (px == null || px.length < 2)
      return;
    String line = train != null ? train.mLine : commuter.mLine;
    String destination = train != null ? train.mDestination : commuter.mDestination;
    String next = train != null ? train.mNextStop : commuter.mNextStop;
    if (line == null)
      line = "";
    if (destination == null)
      destination = "";
    if (next == null)
      next = "";
    String text;
    if (!destination.isEmpty() && !next.isEmpty())
      text = getContext().getString(R.string.metro_train_caption, line, destination, next);
    else if (!destination.isEmpty())
      text = line + " · " + destination;
    else if (!next.isEmpty())
      text = line + " · " + next;
    else
      text = line;
    float pad = 8f * getResources().getDisplayMetrics().density;
    float width = mLabel.measureText(text);
    float left = Math.max(pad, Math.min((float) px[0] + pad, getWidth() - width - pad * 3));
    float top = Math.max(pad, (float) px[1] - mLabel.getTextSize() - pad * 3);
    mLabelBox.set(left, top, left + width + pad * 2, top + mLabel.getTextSize() + pad * 2);
    canvas.drawRoundRect(mLabelBox, pad, pad, mLabelBg);
    canvas.drawText(text, left + pad, top + mLabel.getTextSize() + pad / 2, mLabel);
  }
}
