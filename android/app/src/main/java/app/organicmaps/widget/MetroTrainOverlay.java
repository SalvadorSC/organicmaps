package app.organicmaps.widget;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.AttributeSet;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import app.organicmaps.R;
import app.organicmaps.sdk.Framework;
import app.organicmaps.sdk.metro_live.MetroLine;
import app.organicmaps.sdk.metro_live.MetroLive;
import app.organicmaps.sdk.metro_live.MetroSnapshot;
import app.organicmaps.sdk.metro_live.MetroTrain;
import app.organicmaps.sdk.routing.RoutingController;
import app.organicmaps.sdk.util.NetworkPolicy;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Estimated metro trains drawn over the map. The view does not use Drape: Organic Maps has no
 * moving-marker layer, and a user mark would rebuild the bookmark scene on every frame. Dots are
 * reprojected from the current screen matrix instead.
 */
public class MetroTrainOverlay extends LinearLayout
{
  private static final long REFRESH_MS = 20000;
  private static final long FRAME_MS = 80;
  private static final double SNAP_DEG = 0.02;

  private final Handler mHandler = new Handler(Looper.getMainLooper());
  private final ExecutorService mExecutor = Executors.newSingleThreadExecutor();
  private final AtomicBoolean mBusy = new AtomicBoolean();
  private final Set<String> mDisabled = new HashSet<>();
  private final Map<String, Motion> mMotion = new HashMap<>();
  private final List<Dot> mDots = new ArrayList<>();
  private final Paint mFill = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final Paint mStroke = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final Paint mLabelBg = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final Paint mLabel = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final RectF mLabelBox = new RectF();

  private HorizontalScrollView mChipsScroll;
  private LinearLayout mChips;
  private TextView mBanner;
  private boolean mRunning;
  private boolean mNeedsKey;
  @Nullable
  private String mSelectedKey;
  private float mDownX;
  private float mDownY;
  private boolean mTrackingTrain;
  private final int mTouchSlop;

  private final Runnable mTick = new Runnable()
  {
    @Override
    public void run()
    {
      if (!mRunning)
        return;
      requestPoll();
      mHandler.postDelayed(this, REFRESH_MS);
    }
  };

  private final Runnable mFrame = new Runnable()
  {
    @Override
    public void run()
    {
      if (!mRunning)
        return;
      invalidate();
      mHandler.postDelayed(this, FRAME_MS);
    }
  };

  public MetroTrainOverlay(Context context, @Nullable AttributeSet attrs)
  {
    super(context, attrs);
    setOrientation(VERTICAL);
    setGravity(Gravity.BOTTOM);
    mTouchSlop = ViewConfiguration.get(context).getScaledTouchSlop();
    float density = getResources().getDisplayMetrics().density;
    mStroke.setStyle(Paint.Style.STROKE);
    mStroke.setStrokeWidth(2f * density);
    mStroke.setColor(Color.WHITE);
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
    bannerParams.bottomMargin = (int) (8 * density);
    addView(mBanner, bannerParams);

    View spacer = new View(context);
    addView(spacer, new LayoutParams(LayoutParams.MATCH_PARENT, 0, 1f));

    mChips = new LinearLayout(context);
    mChips.setOrientation(HORIZONTAL);
    mChipsScroll = new HorizontalScrollView(context);
    mChipsScroll.setHorizontalScrollBarEnabled(false);
    mChipsScroll.addView(mChips);
    LayoutParams chipsParams = new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT);
    chipsParams.bottomMargin = (int) (88 * density);
    chipsParams.leftMargin = pad;
    chipsParams.rightMargin = pad;
    addView(mChipsScroll, chipsParams);
    setWillNotDraw(false);
  }

  public void onHostResume()
  {
    mRunning = true;
    refreshVisibility();
    mHandler.removeCallbacks(mTick);
    mHandler.removeCallbacks(mFrame);
    if (getVisibility() == VISIBLE)
    {
      mHandler.post(mTick);
      mHandler.post(mFrame);
    }
  }

  public void onHostPause()
  {
    mRunning = false;
    mHandler.removeCallbacks(mTick);
    mHandler.removeCallbacks(mFrame);
    mBusy.set(false);
  }

  @Override
  protected void onDetachedFromWindow()
  {
    onHostPause();
    mExecutor.shutdownNow();
    super.onDetachedFromWindow();
  }

  private void refreshVisibility()
  {
    boolean show = MetroLive.isEnabled() && !RoutingController.get().isNavigating();
    setVisibility(show ? VISIBLE : GONE);
  }

  private void requestPoll()
  {
    if (!mRunning)
      return;
    refreshVisibility();
    if (getVisibility() != VISIBLE)
      return;
    if (!NetworkPolicy.getCurrentNetworkUsageStatus())
    {
      clearTrains(false);
      return;
    }
    if (!mBusy.compareAndSet(false, true))
      return;
    mExecutor.execute(() -> {
      MetroSnapshot snapshot = null;
      try
      {
        snapshot = MetroLive.poll();
      }
      catch (RuntimeException ignored)
      {
        snapshot = null;
      }
      MetroSnapshot result = snapshot;
      mHandler.post(() -> {
        mBusy.set(false);
        if (!mRunning)
          return;
        apply(result);
      });
    });
  }

  private void clearTrains(boolean needsKey)
  {
    mNeedsKey = needsKey;
    mMotion.clear();
    mSelectedKey = null;
    if (needsKey)
      mBanner.setText(R.string.metro_needs_key);
    else
      mBanner.setText(R.string.metro_estimated);
    mChipsScroll.setVisibility(needsKey ? GONE : VISIBLE);
    invalidate();
  }

  private void apply(@Nullable MetroSnapshot snapshot)
  {
    if (snapshot == null || !snapshot.mEnabled)
    {
      setVisibility(GONE);
      return;
    }
    if (snapshot.mNeedsKey)
    {
      clearTrains(true);
      rebuildChips(snapshot.mLines);
      return;
    }
    mNeedsKey = false;
    mBanner.setText(R.string.metro_estimated);
    mChipsScroll.setVisibility(VISIBLE);
    rebuildChips(snapshot.mLines);
    long now = SystemClock.uptimeMillis();
    Map<String, Motion> next = new HashMap<>();
    if (snapshot.mTrains != null)
    {
      for (MetroTrain train : snapshot.mTrains)
      {
        if (train == null || train.mKey == null)
          continue;
        Motion motion = new Motion();
        motion.mTrain = train;
        motion.mToLat = train.mLat;
        motion.mToLon = train.mLon;
        motion.mStartMs = now;
        Motion previous = mMotion.get(train.mKey);
        if (previous != null)
        {
          double[] shown = previous.display(now);
          if (Math.abs(shown[0] - train.mLat) < SNAP_DEG && Math.abs(shown[1] - train.mLon) < SNAP_DEG)
          {
            motion.mFromLat = shown[0];
            motion.mFromLon = shown[1];
          }
          else
          {
            motion.mFromLat = train.mLat;
            motion.mFromLon = train.mLon;
          }
        }
        else
        {
          motion.mFromLat = train.mLat;
          motion.mFromLon = train.mLon;
        }
        next.put(train.mKey, motion);
      }
    }
    if (mSelectedKey != null && !next.containsKey(mSelectedKey))
      mSelectedKey = null;
    mMotion.clear();
    mMotion.putAll(next);
    invalidate();
  }

  private void rebuildChips(@Nullable MetroLine[] lines)
  {
    if (lines == null)
      return;
    StringBuilder signature = new StringBuilder();
    for (MetroLine line : lines)
      if (line != null && line.mName != null)
        signature.append(line.mName).append('|').append(line.mColor).append(';');
    if (signature.toString().contentEquals(mChips.getTag() == null ? "" : String.valueOf(mChips.getTag())))
      return;
    mChips.setTag(signature.toString());
    mChips.removeAllViews();
    float density = getResources().getDisplayMetrics().density;
    int pad = (int) (10 * density);
    for (MetroLine line : lines)
    {
      if (line == null || line.mName == null)
        continue;
      TextView chip = new TextView(getContext());
      chip.setText(line.mName);
      chip.setPadding(pad, pad / 2, pad, pad / 2);
      int color = parseColor(line.mColor);
      chip.setBackgroundColor(color);
      chip.setTextColor(labelColor(color));
      chip.setAlpha(mDisabled.contains(line.mName) ? 0.35f : 1f);
      LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT);
      params.rightMargin = (int) (6 * density);
      chip.setOnClickListener(v -> {
        if (mDisabled.contains(line.mName))
          mDisabled.remove(line.mName);
        else
          mDisabled.add(line.mName);
        chip.setAlpha(mDisabled.contains(line.mName) ? 0.35f : 1f);
        invalidate();
      });
      mChips.addView(chip, params);
    }
  }

  @Override
  public boolean dispatchTouchEvent(MotionEvent event)
  {
    if (super.dispatchTouchEvent(event))
      return true;
    return onTrainTouch(event);
  }

  private boolean onTrainTouch(MotionEvent event)
  {
    if (mNeedsKey)
      return false;
    float x = event.getX();
    float y = event.getY();
    if (event.getAction() == MotionEvent.ACTION_DOWN)
    {
      mTrackingTrain = hit(x, y) != null;
      mDownX = x;
      mDownY = y;
      return mTrackingTrain;
    }
    if (!mTrackingTrain)
      return false;
    if (event.getAction() == MotionEvent.ACTION_MOVE)
    {
      if (Math.hypot(x - mDownX, y - mDownY) > mTouchSlop)
        mTrackingTrain = false;
      return mTrackingTrain;
    }
    if (event.getAction() == MotionEvent.ACTION_UP)
    {
      Dot dot = hit(x, y);
      mSelectedKey = dot == null ? null : dot.mKey;
      mTrackingTrain = false;
      invalidate();
      return true;
    }
    mTrackingTrain = false;
    return false;
  }

  @Nullable
  private Dot hit(float x, float y)
  {
    float radius = 28f * getResources().getDisplayMetrics().density;
    Dot best = null;
    float bestDist = radius;
    for (Dot dot : mDots)
    {
      float dist = (float) Math.hypot(x - dot.mX, y - dot.mY);
      if (dist <= bestDist)
      {
        best = dot;
        bestDist = dist;
      }
    }
    return best;
  }

  @Override
  protected void onDraw(Canvas canvas)
  {
    super.onDraw(canvas);
    mDots.clear();
    if (mNeedsKey || mMotion.isEmpty())
      return;
    long now = SystemClock.uptimeMillis();
    float radius = 8f * getResources().getDisplayMetrics().density;
    Dot selected = null;
    for (Motion motion : mMotion.values())
    {
      MetroTrain train = motion.mTrain;
      if (train.mLine != null && mDisabled.contains(train.mLine))
        continue;
      double[] shown = motion.display(now);
      double[] px = Framework.nativeLatLonToScreen(shown[0], shown[1]);
      if (px == null || px.length < 2)
        continue;
      if (px[0] < -radius || px[1] < -radius || px[0] > getWidth() + radius || px[1] > getHeight() + radius)
        continue;
      Dot dot = new Dot();
      dot.mX = (float) px[0];
      dot.mY = (float) px[1];
      dot.mKey = train.mKey;
      dot.mTrain = train;
      mDots.add(dot);
      mFill.setColor(parseColor(train.mColor));
      canvas.drawCircle(dot.mX, dot.mY, radius, mFill);
      canvas.drawCircle(dot.mX, dot.mY, radius, mStroke);
      if (train.mKey != null && train.mKey.equals(mSelectedKey))
        selected = dot;
    }
    if (selected != null)
      drawCaption(canvas, selected);
  }

  private void drawCaption(Canvas canvas, Dot dot)
  {
    String destination = dot.mTrain.mDestination == null ? "" : dot.mTrain.mDestination;
    String next = dot.mTrain.mNextStop == null ? "" : dot.mTrain.mNextStop;
    String text = getContext().getString(R.string.metro_train_caption, dot.mTrain.mLine, destination, next);
    float pad = 8f * getResources().getDisplayMetrics().density;
    float width = mLabel.measureText(text);
    float left = Math.max(pad, Math.min(dot.mX + pad, getWidth() - width - pad * 3));
    float top = Math.max(pad, dot.mY - mLabel.getTextSize() - pad * 3);
    mLabelBox.set(left, top, left + width + pad * 2, top + mLabel.getTextSize() + pad * 2);
    canvas.drawRoundRect(mLabelBox, pad, pad, mLabelBg);
    canvas.drawText(text, left + pad, top + mLabel.getTextSize() + pad / 2, mLabel);
  }

  private static int parseColor(@Nullable String color)
  {
    if (color == null)
      return Color.GRAY;
    String hex = color.startsWith("#") ? color : "#" + color;
    try
    {
      return Color.parseColor(hex);
    }
    catch (IllegalArgumentException ignored)
    {
      return Color.GRAY;
    }
  }

  private static int labelColor(int color)
  {
    double luminance = 0.299 * Color.red(color) + 0.587 * Color.green(color) + 0.114 * Color.blue(color);
    return luminance > 160 ? Color.BLACK : Color.WHITE;
  }

  private static final class Motion
  {
    MetroTrain mTrain;
    double mFromLat;
    double mFromLon;
    double mToLat;
    double mToLon;
    long mStartMs;

    double[] display(long now)
    {
      double u = Math.min(1.0, (now - mStartMs) / (double) REFRESH_MS);
      return new double[] {mFromLat + (mToLat - mFromLat) * u, mFromLon + (mToLon - mFromLon) * u};
    }
  }

  private static final class Dot
  {
    float mX;
    float mY;
    String mKey;
    MetroTrain mTrain;
  }

}
