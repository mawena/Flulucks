package com.mawena.fluluks.media

import android.content.Context
import android.graphics.Color
import android.graphics.Rect
import android.graphics.drawable.GradientDrawable
import android.text.TextUtils
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.TextView
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat

/**
 * Couche du lecteur vidéo posée sur le `PlayerView` (phase 12) : barre du haut (titre et boutons, visible avec les
 * commandes du lecteur) et bulle centrale (messages, aperçu d'un glissement). Construite en code, sans gabarit XML.
 */
class VideoOverlay(private val context: Context) {
    val root = FrameLayout(context).apply { setBackgroundColor(Color.BLACK) }
    private val bar = LinearLayout(context).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        background = GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, intArrayOf(0x99000000.toInt(), Color.TRANSPARENT))
        visibility = View.GONE
    }
    private val title = TextView(context).apply {
        setTextColor(Color.WHITE)
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
        maxLines = 1
        ellipsize = TextUtils.TruncateAt.MIDDLE
        setPadding(dp(16), 0, dp(8), 0)
    }
    val rotateButton = button(com.mawena.fluluks.R.drawable.video_ic_rotate, VideoOrientation.ROTATE)
    val lockButton = button(com.mawena.fluluks.R.drawable.video_ic_lock_open, VideoOrientation.lockDescription(OrientationMode.Auto))
    private val info = bubble(20f)
    private val hideInfo = Runnable { info.visibility = View.GONE }

    init {
        bar.addView(title, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        bar.addView(rotateButton)
        bar.addView(lockButton)
        // barre sous les barres système et l'encoche, quelle que soit l'orientation
        ViewCompat.setOnApplyWindowInsetsListener(bar) { v, insets ->
            val i = insets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
            v.setPadding(i.left, i.top, i.right + dp(4), dp(8))
            insets
        }
    }

    /** Pose le lecteur [player] sous la couche. */
    fun attach(player: View) {
        root.addView(player, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT))
        root.addView(bar, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.TOP))
        root.addView(info, FrameLayout.LayoutParams(FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.CENTER))
    }

    fun setTitle(text: String) { title.text = text }

    fun setLocked(locked: Boolean, description: String) {
        lockButton.setImageResource(if (locked) com.mawena.fluluks.R.drawable.video_ic_lock else com.mawena.fluluks.R.drawable.video_ic_lock_open)
        lockButton.contentDescription = description
        lockButton.tooltipText = description
    }

    /** La barre du haut suit les commandes du lecteur. */
    fun setBarVisible(visible: Boolean) { bar.visibility = if (visible) View.VISIBLE else View.GONE }

    /** Bulle centrale ; cachée après [hideAfterMs] ms, ou laissée jusqu'à [hideInfo] si nul. */
    fun showInfo(text: String, hideAfterMs: Long?) {
        info.text = text
        info.visibility = View.VISIBLE
        info.removeCallbacks(hideInfo)
        if (hideAfterMs != null) info.postDelayed(hideInfo, hideAfterMs)
    }

    fun hideInfo() {
        info.removeCallbacks(hideInfo)
        info.visibility = View.GONE
    }

    /** Zones de la couche où un geste ne doit pas partir (barre du haut visible), en coordonnées de la fenêtre. */
    fun controlRects(): List<Rect> = listOfNotNull(windowRect(bar))

    private fun button(icon: Int, description: String) = ImageButton(context).apply {
        setImageResource(icon)
        contentDescription = description
        tooltipText = description
        setBackgroundResource(TypedValue().also {
            context.theme.resolveAttribute(android.R.attr.selectableItemBackgroundBorderless, it, true)
        }.resourceId)
        layoutParams = LinearLayout.LayoutParams(dp(48), dp(48))
    }

    private fun bubble(sizeSp: Float) = TextView(context).apply {
        setTextColor(Color.WHITE)
        setTextSize(TypedValue.COMPLEX_UNIT_SP, sizeSp)
        gravity = Gravity.CENTER
        setPadding(dp(20), dp(12), dp(20), dp(12))
        background = GradientDrawable().apply { setColor(0x99000000.toInt()); cornerRadius = dp(16).toFloat() }
        visibility = View.GONE
        importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_YES
        accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
    }

    private fun dp(v: Int): Int = (v * context.resources.displayMetrics.density).toInt()

    companion object {
        /** Rectangle de [v] dans la fenêtre s'il est affiché, sinon null. */
        fun windowRect(v: View?): Rect? {
            if (v == null || !v.isShown || v.width == 0) return null
            val p = IntArray(2)
            v.getLocationInWindow(p)
            return Rect(p[0], p[1], p[0] + v.width, p[1] + v.height)
        }
    }
}
