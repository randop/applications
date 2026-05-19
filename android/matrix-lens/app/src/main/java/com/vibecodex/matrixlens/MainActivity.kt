package com.vibecodex.matrixlens

import android.Manifest
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.pm.PackageManager
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.History
import androidx.compose.material.icons.filled.QrCodeScanner
import androidx.compose.material.icons.filled.SwapHoriz
import androidx.compose.material3.AssistChip
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Divider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import com.google.mlkit.vision.barcode.Barcode
import com.google.mlkit.vision.barcode.BarcodeScanner
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.barcode.BarcodeScannerOptions
import com.google.mlkit.vision.common.InputImage
import java.util.concurrent.Executors
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

data class ScanHit(
    val value: String,
    val formatLabel: String,
    val timestampMs: Long,
)

data class ScanUiState(
    val permissionGranted: Boolean = false,
    val lastScan: ScanHit? = null,
    val recentScans: List<ScanHit> = emptyList(),
    val scanning: Boolean = false,
    val status: String = "Ready to scan Data Matrix",
)

class ScanViewModel : ViewModel() {
    private val _state = MutableStateFlow(ScanUiState())
    val state: StateFlow<ScanUiState> = _state.asStateFlow()

    private val lastAcceptedAt = mutableMapOf<String, Long>()

    fun setPermissionGranted(granted: Boolean) {
        _state.value = _state.value.copy(permissionGranted = granted)
    }

    fun setScanning(scanning: Boolean) {
        _state.value = _state.value.copy(scanning = scanning)
    }

    fun recordScan(value: String, formatLabel: String) {
        val now = System.currentTimeMillis()
        val previous = lastAcceptedAt[value]
        if (previous != null && now - previous < 1200L) return
        lastAcceptedAt[value] = now

        val hit = ScanHit(
            value = value,
            formatLabel = formatLabel,
            timestampMs = now,
        )
        val updated = listOf(hit) + _state.value.recentScans
        _state.value = _state.value.copy(
            lastScan = hit,
            recentScans = updated.take(25),
            status = "Locked on Data Matrix",
        )
    }

    fun setStatus(status: String) {
        _state.value = _state.value.copy(status = status)
    }
}

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme {
                Surface(color = MaterialTheme.colorScheme.background) {
                    MatrixLensApp()
                }
            }
        }
    }
}

@Composable
private fun MatrixLensApp(
    vm: ScanViewModel = viewModel(factory = object : ViewModelProvider.Factory {
        override fun <T : ViewModel> create(modelClass: Class<T>): T {
            @Suppress("UNCHECKED_CAST")
            return ScanViewModel() as T
        }
    }),
) {
    val context = LocalContext.current
    val state by vm.state.collectAsState()
    var cameraGranted by remember {
        mutableStateOf(
            ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) ==
                PackageManager.PERMISSION_GRANTED,
        )
    }

    LaunchedEffect(cameraGranted) {
        vm.setPermissionGranted(cameraGranted)
    }

    val permissionLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestPermission(),
    ) { granted ->
        cameraGranted = granted
    }

    Scaffold { padding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .background(Color(0xFF090B10)),
        ) {
            if (cameraGranted) {
                CameraScanner(
                    modifier = Modifier.fillMaxSize(),
                    onStatus = vm::setStatus,
                    onBarcode = vm::recordScan,
                    onScanningChanged = vm::setScanning,
                )
            } else {
                PermissionGate(
                    onGrant = {
                        permissionLauncher.launch(Manifest.permission.CAMERA)
                    },
                )
            }

            ScannerOverlay(
                state = state,
                onCopyLatest = {
                    state.lastScan?.let {
                        val clipboard =
                            context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                        clipboard.setPrimaryClip(ClipData.newPlainText("Data Matrix", it.value))
                        Toast.makeText(context, "Copied", Toast.LENGTH_SHORT).show()
                    }
                },
            )
        }
    }
}

@Composable
private fun PermissionGate(onGrant: () -> Unit) {
    Box(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        contentAlignment = Alignment.Center,
    ) {
        Card(
            colors = CardDefaults.cardColors(containerColor = Color(0xFF121622)),
            shape = RoundedCornerShape(24.dp),
        ) {
            Column(
                modifier = Modifier.padding(24.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Text(
                    text = "Camera needed",
                    style = MaterialTheme.typography.headlineSmall,
                    color = Color.White,
                    fontWeight = FontWeight.SemiBold,
                )
                Text(
                    text = "MatrixLens uses camera feed to decode Data Matrix codes in real time.",
                    color = Color(0xFFB6BECE),
                )
                Button(onClick = onGrant) {
                    Text("Grant camera access")
                }
            }
        }
    }
}

@Composable
private fun ScannerOverlay(
    state: ScanUiState,
    onCopyLatest: () -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(18.dp),
        verticalArrangement = Arrangement.SpaceBetween,
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text(
                text = "MatrixLens",
                style = MaterialTheme.typography.headlineMedium,
                color = Color.White,
                fontWeight = FontWeight.Bold,
            )
            Text(
                text = "Fast, tight Data Matrix capture",
                color = Color(0xFFCED6E3),
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                AssistChip(onClick = {}, label = { Text(state.status) }, leadingIcon = {
                    Icon(Icons.Filled.QrCodeScanner, contentDescription = null)
                })
                AssistChip(onClick = {}, label = { Text(if (state.scanning) "Scanning" else "Idle") }, leadingIcon = {
                    Icon(Icons.Filled.History, contentDescription = null)
                })
            }
        }

        Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
            if (state.lastScan != null) {
                Card(
                    colors = CardDefaults.cardColors(containerColor = Color(0xFF141A28)),
                    shape = RoundedCornerShape(20.dp),
                ) {
                    Column(
                        modifier = Modifier.padding(16.dp),
                        verticalArrangement = Arrangement.spacedBy(10.dp),
                    ) {
                        Text(
                            text = "Latest code",
                            color = Color(0xFF9BA7BC),
                            style = MaterialTheme.typography.labelLarge,
                        )
                        Text(
                            text = state.lastScan.value,
                            color = Color.White,
                            fontWeight = FontWeight.SemiBold,
                            maxLines = 3,
                            overflow = TextOverflow.Ellipsis,
                        )
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Text(
                                text = state.lastScan.formatLabel,
                                color = Color(0xFF87D8FF),
                            )
                            IconButton(onClick = onCopyLatest) {
                                Icon(
                                    imageVector = Icons.Filled.ContentCopy,
                                    contentDescription = "Copy latest code",
                                    tint = Color.White,
                                )
                            }
                        }
                    }
                }
            }

            Card(
                colors = CardDefaults.cardColors(containerColor = Color(0xDD0E1220)),
                shape = RoundedCornerShape(20.dp),
            ) {
                Column(modifier = Modifier.padding(12.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(
                            text = "Recent scans",
                            color = Color.White,
                            fontWeight = FontWeight.SemiBold,
                        )
                        Text(
                            text = "${state.recentScans.size}",
                            color = Color(0xFF9BA7BC),
                        )
                    }
                    Spacer(modifier = Modifier.height(8.dp))
                    Divider(color = Color(0xFF223046))
                    Spacer(modifier = Modifier.height(8.dp))
                    LazyColumn(
                        modifier = Modifier.height(200.dp),
                        verticalArrangement = Arrangement.spacedBy(10.dp),
                    ) {
                        items(state.recentScans) { scan ->
                            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                                Text(
                                    text = scan.value,
                                    color = Color.White,
                                    maxLines = 2,
                                    overflow = TextOverflow.Ellipsis,
                                )
                                Text(
                                    text = scan.formatLabel,
                                    color = Color(0xFF9BA7BC),
                                    style = MaterialTheme.typography.labelSmall,
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun CameraScanner(
    modifier: Modifier = Modifier,
    onStatus: (String) -> Unit,
    onBarcode: (String, String) -> Unit,
    onScanningChanged: (Boolean) -> Unit,
) {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current
    val cameraExecutor = remember { Executors.newSingleThreadExecutor() }
    val scanner = remember {
        val options = BarcodeScannerOptions.Builder()
            .setBarcodeFormats(Barcode.FORMAT_DATA_MATRIX)
            .build()
        BarcodeScanning.getClient(options)
    }
    val previewView = remember {
        PreviewView(context).apply {
            scaleType = PreviewView.ScaleType.FILL_CENTER
            implementationMode = PreviewView.ImplementationMode.COMPATIBLE
        }
    }

    AndroidView(
        modifier = modifier,
        factory = { previewView },
    )

    LaunchedEffect(Unit) {
        onScanningChanged(true)
        onStatus("Camera warm")

        val cameraProvider = ProcessCameraProvider.getInstance(context).get()
        val preview = Preview.Builder().build().also {
            it.surfaceProvider = previewView.surfaceProvider
        }

        val analysis = ImageAnalysis.Builder()
            .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
            .build()
            .also { imageAnalysis ->
                imageAnalysis.setAnalyzer(cameraExecutor) { imageProxy ->
                    val mediaImage = imageProxy.image
                    if (mediaImage == null) {
                        imageProxy.close()
                        return@setAnalyzer
                    }

                    val rotation = imageProxy.imageInfo.rotationDegrees
                    val inputImage = InputImage.fromMediaImage(mediaImage, rotation)
                    scanner.process(inputImage)
                        .addOnSuccessListener { barcodes ->
                            val hit = barcodes.firstOrNull { it.format == Barcode.FORMAT_DATA_MATRIX }
                            if (hit != null) {
                                val raw = hit.rawValue ?: hit.displayValue ?: return@addOnSuccessListener
                                onBarcode(raw, "Data Matrix")
                                onStatus("Data Matrix found")
                            } else {
                                onStatus("Searching...")
                            }
                        }
                        .addOnFailureListener { error ->
                            onStatus(error.message ?: "Scan error")
                        }
                        .addOnCompleteListener {
                            imageProxy.close()
                        }
                }
            }

        cameraProvider.unbindAll()
        cameraProvider.bindToLifecycle(
            lifecycleOwner,
            CameraSelector.DEFAULT_BACK_CAMERA,
            preview,
            analysis,
        )
    }

    DisposableEffect(Unit) {
        onDispose {
            cameraExecutor.shutdown()
            scanner.close()
            onScanningChanged(false)
        }
    }
}
